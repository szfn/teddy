#include "jobs.h"

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "baux.h"
#include "editor.h"
#include "columns.h"
#include "lexy.h"
#include "top.h"
#include "global.h"

#define JOBS_READ_BUFFER_SIZE 2048

void jobs_init(void) {
	int i;
	for (i = 0; i < MAX_JOBS; ++i) {
		jobs[i].used = 0;
	}
}

static void job_destroy(job_t *job) {
	g_source_remove(job->pipe_from_child_source_id);

	g_source_remove(job->child_source_id);

	g_io_channel_shutdown(job->pipe_from_child, FALSE, NULL);

	g_io_channel_unref(job->pipe_from_child);
	job->pipe_from_child = NULL;

	job->buffer->job = NULL;

	job->buffer->cursor.line = job->buffer->real_line;
	job->buffer->cursor.glyph = 0;

	editor_t *editor;
	find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
	if (editor != NULL) {
		editor_center_on_cursor(editor);
		gtk_widget_queue_draw(GTK_WIDGET(editor));
	}

	job->used = 0;
}

static void job_append(job_t *job, const char *msg, int len, int on_new_line, uint8_t color) {
	uint8_t saved_color = job->buffer->default_color;
	if (color != 0xff) job->buffer->default_color = color;
	buffer_append(job->buffer, msg, len, on_new_line);
	if (color != 0xff) job->buffer->default_color = saved_color;

	editor_t *editor;
	find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
	if (editor != NULL) {
		editor_center_on_cursor(editor);
		gtk_widget_queue_draw(GTK_WIDGET(editor));
	}
}

static void ansi_append_escape(job_t *job) {
	if (job->ansiseq[0] != '[') return;

	buffer_t *buffer = job->buffer;
	const char command_char = job->ansiseq[job->ansiseq_cap-1];

	if (command_char == 'J') {
		buffer->cursor.line = buffer->real_line;
		buffer->cursor.glyph = 0;
		buffer_set_mark_at_cursor(buffer);
		buffer->mark_transient = true;
		buffer_aux_go_line(buffer, -1);
		buffer_replace_selection(buffer, "");
	} else if (command_char == 'm') {
		if (job->ansiseq_cap != 3) return;
		if (job->ansiseq[1] == '0') {
			job->buffer->default_color = 0;
		} else if (job->ansiseq[1] == '1') {
			job->buffer->default_color = CFG_LEXY_STRING - CFG_LEXY_NOTHING;
		}
	} else {
		job_append(job, "<esc>", strlen("<esc>"), 0, 0xff);

		/*
		printf("esc is [");
		for (int j = esc_start; j <= i; ++j) {
			printf("%c", msg[j]);
		}
		printf("]\n");*/
	}
}

static void ansi_append(job_t *job, const char *msg, int len) {
	buffer_t *buffer = job->buffer;

	int start = 0;
	for (int i = 0; i < len; ++i) {
		switch (job->ansi_state) {
		case ANSI_NORMAL:
			if (msg[i] == 0x0d) {
				job_append(job, msg+start, i - start, 0, 0xff);
				start = i+1;

				buffer->cursor.glyph = 0;
				buffer_set_mark_at_cursor(buffer);
				buffer->mark_transient = true;
				buffer->cursor.glyph = buffer->cursor.line->cap;
				buffer_replace_selection(buffer, "");
			} else if (msg[i] == 0x1b) { /* ANSI escape */
				job_append(job, msg+start, i - start, 0, 0xff);
				job->ansi_state = ANSI_ESCAPE;
				job->ansiseq_cap = 0;
			}
			break;

		case ANSI_ESCAPE:
			if (job->ansiseq_cap >= ANSI_SEQ_MAX_LEN-1) {
				start = i+1;
				job->ansi_state = ANSI_NORMAL;
			} else {
				job->ansiseq[job->ansiseq_cap] = msg[i];
				++(job->ansiseq_cap);
				if (job->ansiseq_cap > 1) {
					if ((msg[i] >= 0x40) && (msg[i] <= 0x7e)) {
						start = i+1;
						job->ansi_state = ANSI_NORMAL;
						job->ansiseq[job->ansiseq_cap] = '\0';
						ansi_append_escape(job);
					}
				}
			}

			break;
		}
	}

	if ((job->ansi_state == ANSI_NORMAL) && (start < len))
		job_append(job, msg+start, len - start, 0, 0xff);
}

static void jobs_child_watch_function(GPid pid, gint status, job_t *job) {
	char *msg;
	asprintf(&msg, "~ %d\n", status);
	job_append(job, msg, strlen(msg), 1, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
	free(msg);
	job_destroy(job);
}

static gboolean jobs_input_watch_function(GIOChannel *source, GIOCondition condition, job_t *job) {
	char buf[JOBS_READ_BUFFER_SIZE];
	char *msg;
	gsize bytes_read;
	GIOStatus r = g_io_channel_read_chars(source, buf, JOBS_READ_BUFFER_SIZE-1, &bytes_read, NULL);

	buf[bytes_read] = '\0';

	if (!job->ratelimit_silenced) {
		ansi_append(job, buf, (size_t)bytes_read);

		if (job->current_ratelimit_bucket_start - time(NULL) > RATELIMIT_BUCKET_DURATION_SECS) {
			if (job->current_ratelimit_bucket_size > RATELIMIT_MAX_BYTES) {
				const char *msg = "SILENCED\n";
				job_append(job, msg, strlen(msg), 1, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
				job->ratelimit_silenced = true;
			}
			job->current_ratelimit_bucket_start = time(NULL);
			job->current_ratelimit_bucket_size = 0;
		}

		job->current_ratelimit_bucket_size += bytes_read;
	}

	switch (r) {
	case G_IO_STATUS_NORMAL:
		return TRUE;
	case G_IO_STATUS_ERROR:
		if (condition & G_IO_HUP) {
			// we don't say nothing here, just wait for death
			/*asprintf(&msg, "~ HUP for PID %d\n", job->child_pid);
			job_append(job, msg, strlen(msg), 1);
			free(msg);*/
		} else {
			asprintf(&msg, "~ (i/o error)\n");
			job_append(job, msg, strlen(msg), 1, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
			free(msg);
		}
		job->child_source_id = g_child_watch_add(job->child_pid, (GChildWatchFunc)jobs_child_watch_function, job);
		return FALSE;
	case G_IO_STATUS_EOF:
		asprintf(&msg, "~ (eof)\n");
		job_append(job, msg, strlen(msg), 1, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
		free(msg);
		job->child_source_id = g_child_watch_add(job->child_pid, (GChildWatchFunc)jobs_child_watch_function, job);
		return FALSE;
	case G_IO_STATUS_AGAIN:
		return TRUE;
	default:
		asprintf(&msg, "~ (unknown error: %d)\n", r);
		job_append(job, msg, strlen(msg), 1, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
		free(msg);
		return FALSE;
	}
}

int jobs_register(pid_t child_pid, int masterfd, struct _buffer_t *buffer, const char *command) {
	int i;
	for (i = 0; i < MAX_JOBS; ++i) {
		if (!(jobs[i].used)) break;
	}

	if (i >= MAX_JOBS) return 0;

	jobs[i].used = 1;
	jobs[i].child_pid = child_pid;
	jobs[i].masterfd = masterfd;
	jobs[i].buffer = buffer;
	jobs[i].buffer->job = jobs+i;

	jobs[i].pipe_from_child = g_io_channel_unix_new(masterfd);
	g_io_channel_set_encoding(jobs[i].pipe_from_child, NULL, NULL);

	jobs[i].ratelimit_silenced = false;
	jobs[i].current_ratelimit_bucket_start = 0;
	jobs[i].current_ratelimit_bucket_size = 0;

	jobs[i].ansi_state = ANSI_NORMAL;

	{
		GError *error = NULL;

		g_io_channel_set_flags(jobs[i].pipe_from_child, g_io_channel_get_flags(jobs[i].pipe_from_child) | G_IO_FLAG_NONBLOCK, &error);
		if (error != NULL) { printf("There was a strange error"); g_error_free(error); error = NULL; }
	}

	jobs[i].pipe_from_child_source_id = g_io_add_watch(jobs[i].pipe_from_child, G_IO_IN|G_IO_HUP, (GIOFunc)(jobs_input_watch_function), jobs+i);

	buffer_aux_clear(buffer);

	char *msg;
	asprintf(&msg, "%% %s\t\t(pid: %d)\n", command, child_pid);
	job_append(jobs+i, msg, strlen(msg), 0, CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING);
	free(msg);

	return 1;
}

int write_all(int fd, const char *str) {
	while (*str != '\0') {
		int r = write(fd, str, strlen(str));
		if (r < 0) return -1;
		str += r;
	}
	return 0;
}


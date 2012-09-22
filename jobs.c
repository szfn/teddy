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
#include "buffers.h"
#include "go.h"

#define JOBS_READ_BUFFER_SIZE 2048
#define SHOWANYWAY_TIMO 500

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

	if (job->buffer != NULL) {
		job->buffer->job = NULL;

		job->buffer->cursor.line = job->buffer->real_line;
		job->buffer->cursor.glyph = 0;

		editor_t *editor;
		find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
		if (editor != NULL) {
			editor_center_on_cursor(editor);
			gtk_widget_queue_draw(GTK_WIDGET(editor));
		}
	}

	job->used = 0;
}

static void job_append(job_t *job, const char *msg, int len, int on_new_line) {
	if (job->buffer == NULL) return;
	buffer_append(job->buffer, msg, len, on_new_line);

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
	if (buffer == NULL) return;
	const char command_char = job->ansiseq[job->ansiseq_cap-1];

	if (command_char == 'J') {
		buffer->cursor.line = buffer->real_line;
		buffer->cursor.glyph = 0;
		buffer_set_mark_at_cursor(buffer);
		buffer_move_point_line(buffer, &(buffer->cursor), MT_END, 0);
		buffer_replace_selection(buffer, "");
	} else if (command_char == 'm') {
		if (job->ansiseq_cap != 3) return;
		if (job->ansiseq[1] == '0') {
			job->buffer->default_color = 0;
		} else if (job->ansiseq[1] == '1') {
			job->buffer->default_color = CFG_LEXY_STRING - CFG_LEXY_NOTHING;
		}
	} else {
		job_append(job, "<esc>", strlen("<esc>"), 0);
	}
}

static void ansi_append(job_t *job, const char *msg, int len) {
	buffer_t *buffer = job->buffer;
	if (buffer == NULL) return;

	int start = 0;
	for (int i = 0; i < len; ++i) {
		switch (job->ansi_state) {
		case ANSI_NORMAL:
			if (msg[i] == 0x0d) {
				job_append(job, msg+start, i - start, 0);
				start = i+1;

				buffer->cursor.glyph = 0;
				buffer_set_mark_at_cursor(buffer);
				buffer->cursor.glyph = buffer->cursor.line->cap;
				buffer_replace_selection(buffer, "");
			} else if (msg[i] == 0x1b) { /* ANSI escape */
				job_append(job, msg+start, i - start, 0);
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
		job_append(job, msg+start, len - start, 0);
}

static void jobs_child_watch_function(GPid pid, gint status, job_t *job) {
	if ((job->buffer == NULL) || (job->buffer->path[0] != '+')) {
		char *msg;
		asprintf(&msg, "~ %d\n", status);
		job_append(job, msg, strlen(msg), 1);
		free(msg);
	}
	job_destroy(job);
}


static void job_attach_to_buffer(job_t *job, const char *command, buffer_t *buffer) {
	if (job->buffer != NULL) return;
	job->buffer = buffer;
	job->buffer->job = job;

	buffer_aux_clear(buffer);

	if ((command != NULL) && (buffer->path[0] == '+')) {
		char *msg;
		asprintf(&msg, "%% %s\n", command);
		job_append(job, msg, strlen(msg), 0);
		free(msg);
	}
}

static void job_create_buffer(job_t *job) {
	if (job->buffer != NULL) return;
	if (job->terminating) return;

	buffer_t *buffer = buffers_get_buffer_for_process();
	job_attach_to_buffer(job, job->command, buffer);
	go_to_buffer(NULL, buffer, false);
	free(job->command);
	job->command = NULL;
}

static gboolean jobs_input_watch_function(GIOChannel *source, GIOCondition condition, job_t *job) {
	char buf[JOBS_READ_BUFFER_SIZE];
	char *msg;
	gsize bytes_read;
	GIOStatus r = g_io_channel_read_chars(source, buf, JOBS_READ_BUFFER_SIZE-1, &bytes_read, NULL);

	buf[bytes_read] = '\0';

	if (r != G_IO_STATUS_EOF) {
		job_create_buffer(job);
	}

	if (!job->ratelimit_silenced) {
		ansi_append(job, buf, (size_t)bytes_read);

		if (job->current_ratelimit_bucket_start - time(NULL) > RATELIMIT_BUCKET_DURATION_SECS) {
			if (job->current_ratelimit_bucket_size > RATELIMIT_MAX_BYTES) {
				const char *msg = "SILENCED\n";
				job_append(job, msg, strlen(msg), 1);
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
		} else {
			asprintf(&msg, "~ (i/o error)\n");
			job_append(job, msg, strlen(msg), 1);
			free(msg);
		}
		job->child_source_id = g_child_watch_add(job->child_pid, (GChildWatchFunc)jobs_child_watch_function, job);
		return FALSE;
	case G_IO_STATUS_EOF:
		asprintf(&msg, "~ (eof)\n");
		job_append(job, msg, strlen(msg), 1);
		free(msg);
		job->child_source_id = g_child_watch_add(job->child_pid, (GChildWatchFunc)jobs_child_watch_function, job);
		return FALSE;
	case G_IO_STATUS_AGAIN:
		return TRUE;
	default:
		asprintf(&msg, "~ (unknown error: %d)\n", r);
		job_append(job, msg, strlen(msg), 1);
		free(msg);
		return FALSE;
	}
}

static gboolean autoshow_job_buffer(job_t *job) {
	job_create_buffer(job);
	return FALSE;
}

int jobs_register(pid_t child_pid, int masterfd, buffer_t *buffer, const char *command) {
	int i;
	for (i = 0; i < MAX_JOBS; ++i) {
		if (!(jobs[i].used)) break;
	}

	if (i >= MAX_JOBS) return 0;

	jobs[i].used = 1;
	jobs[i].child_pid = child_pid;
	jobs[i].masterfd = masterfd;
	jobs[i].buffer = NULL;
	jobs[i].terminating = false;

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

	if (buffer != NULL) {
		job_attach_to_buffer(jobs+i, command, buffer);
	} else {
		jobs[i].command = strdup(command);
		g_timeout_add(SHOWANYWAY_TIMO, (GSourceFunc)autoshow_job_buffer, (gpointer)(jobs+i));
	}

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


#include "jobs.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "editor.h"
#include "columns.h"
#include "lexy.h"
#include "top.h"
#include "global.h"
#include "buffers.h"

#define JOBS_READ_BUFFER_SIZE 2048
#define SHOWANYWAY_TIMO 500

void jobs_init(void) {
	int i;
	for (i = 0; i < MAX_JOBS; ++i) {
		jobs[i].used = 0;
	}
}

static int write_all(int fd, const char *str) {
	while (*str != '\0') {
		int r = write(fd, str, strlen(str));
		if (r < 0) return -1;
		str += r;
	}
	return 0;
}

static char *cut_prompt(job_t *job) {
	int start;

	job->buffer->cursor = BSIZE(job->buffer);

	for (start = job->buffer->cursor-1; start > 0; --start) {
		my_glyph_info_t *g = bat(job->buffer, start);
		if (g == NULL) return NULL;
		if (g->code == '\n') break;
		if (g->code == '\05') break;
	}

	if (start < 0) return NULL;

	job->buffer->mark = start;

	return buffer_lines_to_text(job->buffer, start, job->buffer->cursor);
}

static void job_destroy(job_t *job) {
	g_source_remove(job->pipe_from_child_source_id);
	g_source_remove(job->child_source_id);
	g_io_channel_shutdown(job->pipe_from_child, FALSE, NULL);
	g_io_channel_unref(job->pipe_from_child);
	job->pipe_from_child = NULL;

	if (job->buffer != NULL) {
		job->buffer->job = NULL;

		char *p = cut_prompt(job);
		if (p != NULL) free(p);
		buffer_replace_selection(job->buffer, "");

		job->buffer->cursor = 0;

		editor_t *editor;
		find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
		if (editor != NULL) {
			editor_include_cursor(editor, ICM_MID, ICM_MID);
			gtk_widget_queue_draw(GTK_WIDGET(editor));
		}
	}

	free(job->command);
	job->command = NULL;

	job->used = 0;
}

static void job_append(job_t *job, const char *msg, int len, int on_new_line) {
	if (job->buffer == NULL) return;
	buffer_t *buffer = job->buffer;

	//buffer_append(job->buffer, msg, len, on_new_line);

	char *prompt_str = cut_prompt(job);
	//printf("prompt_str <%s>\n", prompt_str);

	if (on_new_line) {
		my_glyph_info_t *glyph = bat(buffer, buffer->cursor);
		if ((glyph != NULL) && (glyph->code != '\n')) {
			buffer_replace_selection(buffer, "\n");
		}
	}

	char *text = malloc(sizeof(char) * (len + 1));
	alloc_assert(text);
	strncpy(text, msg, len);
	text[len] = '\0';

	buffer_replace_selection(buffer, text);

	free(text);

	if (prompt_str != NULL) {
		buffer_replace_selection(buffer, prompt_str);
		free(prompt_str);
	} else {
		buffer_replace_selection(buffer, "\05");
	}

	editor_t *editor;
	find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
	if (editor != NULL) {
		editor_include_cursor(editor, ICM_MID, ICM_MID);
		gtk_widget_queue_draw(GTK_WIDGET(editor));
	}
}

void job_send_input(job_t *job) {
	if (job->buffer == NULL) return;
	char *input = cut_prompt(job);
	//printf("input <%s>\n", input);
	job->buffer->mark = -1;
	buffer_replace_selection(job->buffer, "\n\05");
	if (input == NULL) return;

	write_all(job->masterfd, input+1);
	write_all(job->masterfd, "\n");
	free(input);
}

static void ansi_append_escape(job_t *job) {
	if (job->ansiseq[0] != '[') return;

	buffer_t *buffer = job->buffer;
	if (buffer == NULL) return;
	const char command_char = job->ansiseq[job->ansiseq_cap-1];

	if (command_char == 'J') {
		buffer->mark = buffer->cursor = 0;
		buffer_move_point_line(buffer, &(buffer->cursor), MT_END, 0);
		buffer_replace_selection(buffer, "");
	} else if (command_char == 'm') {
		if (job->ansiseq_cap != 3) return;
		if (job->ansiseq[1] == '0') {
			job->buffer->default_color = 0;
		} else if (job->ansiseq[1] == '1') {
			job->buffer->default_color = CFG_LEXY_STRING - CFG_LEXY_NOTHING;
		}
	} else if (command_char == 'H') {
		buffer_move_point_line(buffer, &(buffer->cursor), MT_ABS, 1);
		buffer_move_point_glyph(buffer, &(buffer->cursor), MT_ABS, 1);
		editor_t *editor;
		find_editor_for_buffer(buffer, NULL, NULL, &editor);
		if (editor != NULL) editor_include_cursor(editor, ICM_MID, ICM_MID);
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

				job->buffer->mark = job->buffer->cursor = BSIZE(job->buffer)-1;
				buffer_move_point_glyph(buffer, &(job->buffer->mark), MT_ABS, 1);
				buffer_replace_selection(buffer, "");
			} else if (msg[i] == 0x08) {
				job_append(job, msg+start, i - start, 0);
				start = i+1;
				job->buffer->mark = job->buffer->cursor;
				buffer_move_point_glyph(buffer, &(job->buffer->mark), MT_REL, -1);
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

static void job_lexy_refresh(job_t *job) {
	//printf("Job finished waiting for lexy to refresh: %p\n", job->buffer);
	if (job->buffer == NULL) return;

	editor_t *editor;
	find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
	//printf("\teditor: %p\n", editor);
	if (editor == NULL) return;

	for (int count = 0; job->buffer->lexy_running != 0; ++count) {
		//printf("Lexy is running: %d\n", job->buffer->lexy_running);
		if (count > 5) return;

		struct timespec s;
		s.tv_sec = 0;
		s.tv_nsec = 10000000;
		nanosleep(&s, NULL);
	}

	//printf("Issuing redraw\n");

	gtk_widget_queue_draw(editor->drar);
}

static void jobs_child_watch_function(GPid pid, gint status, job_t *job) {
	if ((job->buffer != NULL) && (job->buffer->path[0] == '+')) {
		char *msg;
		asprintf(&msg, "~ %d\n", status);
		job_append(job, msg, strlen(msg), 1);
		free(msg);
	}
	job_lexy_refresh(job);
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

	// setting terminal size

	editor_t *editor;
	find_editor_for_buffer(buffer, NULL, NULL, &editor);
	if (editor != NULL) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(editor), &allocation);
		double width = allocation.width;

		struct winsize {
			unsigned short ws_row;
			unsigned short ws_col;
			unsigned short ws_xpixel;
			unsigned short ws_ypixel;
		} ws;
		ws.ws_row = 100;
		ws.ws_col = (int)(width / (0.75 * buffer->em_advance));
		if (ws.ws_col > 0) {
			ioctl(job->masterfd, TIOCSWINSZ, &ws);
		}
	}
}

static void job_create_buffer(job_t *job) {
	if (job->buffer != NULL) return;
	if (job->terminating) return;

	buffer_t *buffer = buffers_get_buffer_for_process();
	job_attach_to_buffer(job, job->command, buffer);
	go_to_buffer(NULL, buffer, false);
}

static gboolean jobs_input_watch_function(GIOChannel *source, GIOCondition condition, job_t *job) {
	char buf[JOBS_READ_BUFFER_SIZE];
	char *msg;
	gsize bytes_read;
	GIOStatus r = g_io_channel_read_chars(source, buf, JOBS_READ_BUFFER_SIZE-1, &bytes_read, NULL);

	buf[bytes_read] = '\0';

	if (r != G_IO_STATUS_EOF) {
		if ((r != G_IO_STATUS_ERROR) || !(condition & G_IO_HUP)) {
			//printf("job_create_buffer from jobs_input_watch %d %d\n", r, G_IO_STATUS_NORMAL);
			job_create_buffer(job);
		}
	}

	if (!job->ratelimit_silenced) {
		ansi_append(job, buf, (size_t)bytes_read);

		if (time(NULL) - job->current_ratelimit_bucket_start > RATELIMIT_BUCKET_DURATION_SECS) {
			//printf("Ratelimit bucket size: %ld\n", job->current_ratelimit_bucket_size);
			if (job->current_ratelimit_bucket_size > RATELIMIT_MAX_BYTES) {
				const char *msg = "\nRATELIMIT REACHED - SILENCED\n";
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
	if (job->pipe_from_child == NULL) return FALSE;
	//printf("Autoshow\n");
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
	jobs[i].command = strdup(command);

	if (buffer != NULL) {
		job_attach_to_buffer(jobs+i, command, buffer);
	} else {
		g_timeout_add(SHOWANYWAY_TIMO, (GSourceFunc)autoshow_job_buffer, (gpointer)(jobs+i));
	}

	return 1;
}

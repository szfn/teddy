#include "jobs.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "editor.h"
#include "columns.h"
#include "lexy.h"
#include "top.h"
#include "global.h"
#include "buffers.h"
#include "ipc.h"

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
	if (job->buffer == NULL) return NULL;
	job->buffer->cursor = BSIZE(job->buffer);
	job->buffer->mark = job->buffer->appjumps[APPJUMP_INPUT];
	if (job->buffer->mark < 0) return NULL;
	char *r = buffer_lines_to_text(job->buffer, job->buffer->mark, job->buffer->cursor);
	//printf("Cut prompt: <%s> %d\n", r, strlen(r));
	return r;
}

static void job_destroy(job_t *job) {
	g_source_remove(job->pipe_from_child_source_id);
	g_source_remove(job->child_source_id);
	g_io_channel_shutdown(job->pipe_from_child, FALSE, NULL);
	g_io_channel_unref(job->pipe_from_child);
	job->pipe_from_child = NULL;

	if (job->buffer != NULL) {
		job->buffer->job = NULL;

		job->buffer->cursor = job->reset_position;

		editor_t *editor;
		find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
		if (editor != NULL) {
			editor_include_cursor(editor, ICM_MID, ICM_MID);
			gtk_widget_queue_draw(GTK_WIDGET(editor));
		}
	}

	if ((job->directory != NULL) && (job->buffer != NULL)) {
		if (job->buffer->wd != NULL) free(job->buffer->wd);
		job->buffer->wd = strdup(job->directory);
	}

	free(job->command);
	free(job->directory);
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

	//printf("jobappending <%s> (%d:%d <%s>)\n", text, buffer->mark, buffer->cursor, prompt_str);

	buffer_replace_selection(buffer, text);

	free(text);

	if (prompt_str != NULL) {
		buffer_replace_selection(buffer, prompt_str);
		free(prompt_str);
	}

	buffer->appjumps[APPJUMP_INPUT] = buffer->cursor;

	editor_t *editor;
	find_editor_for_buffer(job->buffer, NULL, NULL, &editor);
	if (editor != NULL) {
		editor_include_cursor(editor, ICM_MID, ICM_MID);
		gtk_widget_queue_draw(GTK_WIDGET(editor));
	}
}

static gboolean job_update_directory(job_t  *job) {
	if (job->buffer == NULL) return FALSE;
	pid_t pid = tcgetpgrp(job->masterfd);
	if (pid < 0) {
		pid = job->child_pid;
	}
	if (pid < 0) return FALSE;

	char *cwdf;
	asprintf(&cwdf, "/proc/%d/cwd", pid);
	alloc_assert(cwdf);

	char dest[MAXPATHLEN];
	alloc_assert(dest);

	int r = readlink(cwdf, dest, MAXPATHLEN-1);
	if (r < 0) {
		free(cwdf);
		return FALSE;
	}

	dest[r] = '\0';

	free(cwdf);

	if (job->directory != NULL) free(job->directory);
	job->directory = strdup(dest);

	if (job->buffer->wd != NULL) free(job->buffer->wd);
	job->buffer->wd = strdup(job->directory);
	alloc_assert(job->buffer->wd);

	tframe_t *frame;
	find_editor_for_buffer(job->buffer, NULL, &frame, NULL);
	if (frame != NULL) {
		tframe_set_wd(frame, job->directory);
		gtk_widget_queue_draw(GTK_WIDGET(frame));
	}

	return FALSE;
}

void job_send_input(job_t *job, const char *actual_input) {
	if (job->buffer == NULL) return;
	char *input = cut_prompt(job);
	//printf("input <%s>\n", input);
	job->buffer->mark = -1;
	buffer_replace_selection(job->buffer, "\n");
	job->buffer->appjumps[APPJUMP_INPUT] = job->buffer->cursor;

	const char *send_input = NULL;

	if (actual_input != NULL) {
		send_input = actual_input;
	} else {
		if (input == NULL) return;
		send_input = input;
	}

	if (send_input == NULL) return;

	//printf("Sending input <%s>\n", send_input);

	write_all(job->masterfd, send_input);
	write_all(job->masterfd, "\n");
	history_add(&input_history, time(NULL), NULL, send_input, true);
	if (input != NULL) free(input);
	job->ratelimit_silenced = false;

	g_timeout_add(SHOWANYWAY_TIMO*2, (GSourceFunc)job_update_directory, job);
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
		//job_append(job, "<esc>", strlen("<esc>"), 0);
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
				job->ansi_state = ANSI_0D;
			} else if (msg[i] == 0x08) {
				job_append(job, msg+start, i - start, 0);
				start = i+1;
				char *p = cut_prompt(job);
				if (p != NULL) free(p);
				if (job->buffer->mark < 0) job->buffer->mark = job->buffer->cursor;
				buffer_move_point_glyph(buffer, &(job->buffer->mark), MT_REL, -1);
				buffer_replace_selection(buffer, "");
			} else if (msg[i] == 0x1b) { /* ANSI escape */
				job_append(job, msg+start, i - start, 0);
				job->ansi_state = ANSI_ESCAPE;
				job->ansiseq_cap = 0;
			}
			break;

		case ANSI_0D:
			if (msg[i] == 0x0a) {
				job->ansi_state = ANSI_NORMAL;
			} else {
				job->buffer->mark = job->buffer->cursor = BSIZE(job->buffer)-1;
				buffer_move_point_glyph(buffer, &(job->buffer->mark), MT_ABS, 1);
				buffer_replace_selection(buffer, "");
				job->ansi_state = ANSI_NORMAL;
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

	int sb = config_intval(&global_config, CFG_JOBS_SCROLLBACK);
	if (sb == 0) {
		buffer_aux_clear(buffer);
	} else {
		if (BSIZE(buffer) > sb) {
			buffer->mark = 0;
			buffer->cursor = BSIZE(buffer) - sb - 1;
			buffer_replace_selection(buffer, "");
		}
		buffer->mark = -1;
		buffer->cursor = BSIZE(buffer);
	}

	job->reset_position = buffer->cursor;

	if ((command != NULL) && (buffer->path[0] == '+')) {
		char *msg;
#define TRUNCATION 120
		if (strlen(command) > TRUNCATION) {
			char *c = strndup(command, TRUNCATION-1);
			asprintf(&msg, "%% %s...\n", c);
			free(c);
		} else {
			asprintf(&msg, "%% %s\n", command);
		}
		job_append(job, msg, strlen(msg), 0);
		free(msg);
	}

	// set current directory

	if (buffer->wd != NULL) free(buffer->wd);
	buffer->wd = strdup(job->directory);
	alloc_assert(buffer->wd);

	// setting terminal size

	editor_t *editor;
	column_t *column;
	tframe_t *frame;
	find_editor_for_buffer(buffer, &column, &frame, &editor);
	if (editor != NULL) {
		set_label_text(editor);

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
		ws.ws_col = (int)(width / (0.50 * buffer->em_advance));
		ws.ws_xpixel = 0;
		ws.ws_ypixel = 0;
		if (ws.ws_col > 0) {
			ioctl(job->masterfd, TIOCSWINSZ, &ws);
		}

		if ((column != NULL) && (frame != NULL)) {
			column_expand_frame(column, frame);
		}
	}

	// job notification
	char *msg;
	asprintf(&msg, "j %s", command);
	mq_broadcast(&buffer->watchers, msg);
	free(msg);
}

static void job_create_buffer(job_t *job) {
	if (job->buffer != NULL) return;
	if (job->terminating) return;
	if (job->never_attach) return;

	buffer_t *buffer = buffers_get_buffer_for_process(true);
	job_attach_to_buffer(job, job->command, buffer);
	go_to_buffer(NULL, buffer, false);
}

static gboolean jobs_input_watch_function(GIOChannel *source, GIOCondition condition, job_t *job) {
	char buf[JOBS_READ_BUFFER_SIZE];
	char *msg;
	gsize bytes_read;

	strcpy(buf, job->utf8annoyance);

	GIOStatus r = g_io_channel_read_chars(source, buf+strlen(job->utf8annoyance), JOBS_READ_BUFFER_SIZE-1-strlen(job->utf8annoyance), &bytes_read, NULL);
	bytes_read += strlen(job->utf8annoyance);

	buf[bytes_read] = '\0';

	if ((bytes_read > 0) && job->first_read) {
		job->first_read = false;
		if (buf[0] == 0x00) {
			job->never_attach = true;
		}
	}

	if (r != G_IO_STATUS_EOF) {
		if ((r != G_IO_STATUS_ERROR) || !(condition & G_IO_HUP)) {
			//printf("job_create_buffer from jobs_input_watch %d %d\n", r, G_IO_STATUS_NORMAL);
			job_create_buffer(job);
		}
	}

	if (bytes_read > 0) {
		int k = utf8_excision(buf, bytes_read);
		int charlen = utf8_first_byte_processing(buf[k]);
		if ((k+charlen >= bytes_read) && (charlen < 8)) {
			strcpy(job->utf8annoyance, buf+k);
			buf[k] = '\0';
			bytes_read = k;
		} else {
			job->utf8annoyance[0] = '\0';
		}
	}

	for (int i = 0; i < bytes_read; ++i) {
		buf[i] = (buf[i] == '\0') ? 0x01 : buf[i];
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

	jobs[i].directory = get_current_dir_name();

	jobs[i].used = 1;
	jobs[i].child_pid = child_pid;
	jobs[i].masterfd = masterfd;
	jobs[i].buffer = NULL;
	jobs[i].terminating = false;

	jobs[i].utf8annoyance[0] = '\0';

	jobs[i].pipe_from_child = g_io_channel_unix_new(masterfd);
	g_io_channel_set_encoding(jobs[i].pipe_from_child, NULL, NULL);

	jobs[i].ratelimit_silenced = false;
	jobs[i].current_ratelimit_bucket_start = 0;
	jobs[i].current_ratelimit_bucket_size = 0;

	jobs[i].ansi_state = ANSI_NORMAL;

	jobs[i].reset_position = 0;

	jobs[i].first_read = true;
	jobs[i].never_attach = false;

	{
		GError *error = NULL;

		g_io_channel_set_flags(jobs[i].pipe_from_child, g_io_channel_get_flags(jobs[i].pipe_from_child) | G_IO_FLAG_NONBLOCK, &error);
		if (error != NULL) { printf("There was a strange error"); g_error_free(error); error = NULL; }
	}

	jobs[i].pipe_from_child_source_id = g_io_add_watch(jobs[i].pipe_from_child, G_IO_IN|G_IO_HUP, (GIOFunc)(jobs_input_watch_function), jobs+i);
	jobs[i].command = strdup(command);

	if (buffer != NULL) {
		if (buffer == buffers[0]) {
			jobs[i].never_attach = true;
		} else {
			job_attach_to_buffer(jobs+i, command, buffer);
		}
	} else {
		g_timeout_add(SHOWANYWAY_TIMO, (GSourceFunc)autoshow_job_buffer, (gpointer)(jobs+i));
	}

	return 1;
}

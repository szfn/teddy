#include "jobs.h"

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "baux.h"
#include "editor.h"
#include "columns.h"

#define JOBS_READ_BUFFER_SIZE 128

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

    job->used = 0;
}

static gboolean jobs_input_watch_function(GIOChannel *source, GIOCondition condition, job_t *job) {
    char buf[JOBS_READ_BUFFER_SIZE];
    char *msg;
    gsize bytes_read;
    int i;
    GIOStatus r = g_io_channel_read_chars(source, buf, JOBS_READ_BUFFER_SIZE-1, &bytes_read, NULL);

    buf[bytes_read] = '\0';

    for (i = 0; i < bytes_read; ++i) {
        printf("%d ", buf[i]);
    }
    printf("\n");

    buffer_append(job->buffer, buf, (size_t)bytes_read, 0);
    {
        editor_t *editor = columns_get_buffer(job->buffer);
        if (editor != NULL) {
            editor_center_on_cursor(editor);
        }
    }

    switch (r) {
    case G_IO_STATUS_NORMAL:
        return TRUE;
    case G_IO_STATUS_ERROR:
        asprintf(&msg, "~ Error for PID %d\n", job->child_pid);
        buffer_append(job->buffer, msg, strlen(msg), 1);
        free(msg);
        return FALSE;
    case G_IO_STATUS_EOF:
        asprintf(&msg, "~ EOF for PID %d\n", job->child_pid);
        buffer_append(job->buffer, msg, strlen(msg), 1);
        free(msg);
        return FALSE;
    case G_IO_STATUS_AGAIN:
        return TRUE;
    default:
        asprintf(&msg, "~ Unexpected error for PID %d (%d)\n", job->child_pid, r);
        buffer_append(job->buffer, msg, strlen(msg), 1);
        free(msg);
        return FALSE;
    }
}

static void jobs_child_watch_function(GPid pid, gint status, job_t *job) {
    char *msg;
    asprintf(&msg, "~ Process PID %d ended (status: %d)\n", job->child_pid, status);
    buffer_append(job->buffer, msg, strlen(msg), 1);
    free(msg);
    job_destroy(job);
}

int jobs_register(pid_t child_pid, int masterfd, struct _buffer_t *buffer) {
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

    {
        GError *error = NULL;
        
        g_io_channel_set_flags(jobs[i].pipe_from_child, g_io_channel_get_flags(jobs[i].pipe_from_child) | G_IO_FLAG_NONBLOCK, &error);
        if (error != NULL) { printf("There was a strange error"); g_error_free(error); error = NULL; }
    }

    jobs[i].pipe_from_child_source_id = g_io_add_watch(jobs[i].pipe_from_child, G_IO_IN|G_IO_HUP, (GIOFunc)(jobs_input_watch_function), jobs+i);
    
    jobs[i].child_source_id = g_child_watch_add(child_pid, (GChildWatchFunc)jobs_child_watch_function, jobs+i);

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


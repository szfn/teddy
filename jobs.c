#include "jobs.h"

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#define JOBS_READ_BUFFER_SIZE 128

void jobs_init(void) {
    int i;
    for (i = 0; i < MAX_JOBS; ++i) {
        jobs[i].used = 0;
    }
}

static void job_destroy(job_t *job) {
    g_source_remove(job->pipe_from_child_source_id);
    g_source_remove(job->pipe_err_child_source_id);

    g_source_remove(job->child_source_id);

    g_io_channel_shutdown(job->pipe_from_child, FALSE, NULL);
    g_io_channel_shutdown(job->pipe_err_child, FALSE, NULL);

    close(job->pipe_to_child);

    g_io_channel_unref(job->pipe_from_child);
    job->pipe_from_child = NULL;

    g_io_channel_unref(job->pipe_err_child);
    job->pipe_err_child = NULL;

    job->used = 0;
}

static gboolean jobs_input_watch_function(GIOChannel *source, GIOCondition condition, job_t *job) {
    char buf[JOBS_READ_BUFFER_SIZE];
    gsize bytes_read;
    GIOStatus r = g_io_channel_read_chars(source, buf, JOBS_READ_BUFFER_SIZE-1, &bytes_read, NULL);

    buf[bytes_read] = '\0';

    printf("%d -> %s [%d]\n", job->child_pid, buf, (int)bytes_read);

    if (r != G_IO_STATUS_NORMAL) {
        printf("~ Error reading pid: %d (%d)\n", job->child_pid, r);
        return FALSE;
    }
    
    return TRUE;
}

static void jobs_child_watch_function(GPid pid, gint status, job_t *job) {
    printf("Job status %d -> %d\n", job->child_pid, status);
    job_destroy(job);
}

int jobs_register(pid_t child_pid, int pipe_from_child, int pipe_to_child, int pipe_err_child) {
    int i;
    for (i = 0; i < MAX_JOBS; ++i) {
        if (!(jobs[i].used)) break;

    }

    if (i >= MAX_JOBS) return 0;

    jobs[i].used = 1;
    jobs[i].child_pid = child_pid;
    jobs[i].pipe_to_child = pipe_to_child;
    
    jobs[i].pipe_from_child = g_io_channel_unix_new(pipe_from_child);
    jobs[i].pipe_err_child = g_io_channel_unix_new(pipe_err_child);

    {
        GError *error = NULL;
        
        g_io_channel_set_flags(jobs[i].pipe_from_child, g_io_channel_get_flags(jobs[i].pipe_from_child) | G_IO_FLAG_NONBLOCK, &error);
        if (error != NULL) { printf("There was a strange error"); g_error_free(error); error = NULL; }

        g_io_channel_set_flags(jobs[i].pipe_err_child, g_io_channel_get_flags(jobs[i].pipe_err_child) | G_IO_FLAG_NONBLOCK, &error);
        if (error != NULL) { printf("There was a strange error (2)"); g_error_free(error); error = NULL; }
    }

    jobs[i].pipe_from_child_source_id = g_io_add_watch(jobs[i].pipe_from_child, G_IO_IN|G_IO_HUP, (GIOFunc)(jobs_input_watch_function), jobs+i);
    
    jobs[i].pipe_err_child_source_id = g_io_add_watch(jobs[i].pipe_err_child, G_IO_IN|G_IO_HUP, (GIOFunc)(jobs_input_watch_function), jobs+i);

    jobs[i].child_source_id = g_child_watch_add(child_pid, (GChildWatchFunc)jobs_child_watch_function, jobs+i);

    //TODO: add pid watch

    return 1;
}

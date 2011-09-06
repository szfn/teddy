#ifndef __JOBS_H__
#define __JOBS_H__

#include <unistd.h>
#include <glib.h>

typedef struct _job_t {
    int used;
    pid_t child_pid;
    int pipe_to_child;
    
    GIOChannel *pipe_from_child;
    GIOChannel *pipe_err_child;

    guint child_source_id;
    guint pipe_from_child_source_id;
    guint pipe_err_child_source_id;
} job_t;

#define MAX_JOBS 128

job_t jobs[MAX_JOBS];

void jobs_init(void);
int jobs_register(pid_t child_pid, int pipe_from_child, int pipe_to_child, int pipe_err_child);

#endif

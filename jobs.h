#ifndef __JOBS_H__
#define __JOBS_H__

#include <unistd.h>
#include <glib.h>

struct _buffer_t;

typedef struct _job_t {
    int used;
    pid_t child_pid;
    struct _buffer_t *buffer;
    
    int masterfd;    
    GIOChannel *pipe_from_child;

    guint child_source_id;
    guint pipe_from_child_source_id;
} job_t;

#define MAX_JOBS 128

job_t jobs[MAX_JOBS];

void jobs_init(void);
int jobs_register(pid_t child_pid, int masterfd, struct _buffer_t *buffer);

#endif

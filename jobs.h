#ifndef __JOBS_H__
#define __JOBS_H__

#include <unistd.h>
#include <glib.h>
#include <time.h>
#include <stdbool.h>

struct _buffer_t;

#define RATELIMIT_BUCKET_DURATION_SECS 5
#define RATELIMIT_MAX_BYTES 10000

typedef struct _job_t {
	int used;
	pid_t child_pid;
	struct _buffer_t *buffer;

	int masterfd;
	GIOChannel *pipe_from_child;

	guint child_source_id;
	guint pipe_from_child_source_id;

	bool ratelimit_silenced;
	time_t current_ratelimit_bucket_start;
	long current_ratelimit_bucket_size;
} job_t;

#define MAX_JOBS 128

job_t jobs[MAX_JOBS];

void jobs_init(void);
int jobs_register(pid_t child_pid, int masterfd, struct _buffer_t *buffer, const char *command);
int write_all(int fd, const char *str);

#endif

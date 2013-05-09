#ifndef __MULTIQUEUE__
#define __MULTIQUEUE__

#include <stdbool.h>

#include <glib.h>

struct multiqueue {
	int n;
	int reg;
	bool open;
	pthread_mutex_t mutex;
	pthread_cond_t exitcond;
	GAsyncQueue **queues;
};

void mq_alloc(struct multiqueue *mq, int n);
GAsyncQueue *mq_register(struct multiqueue *mq);
void mq_remove(struct multiqueue *mq, GAsyncQueue *q);
void mq_broadcast(struct multiqueue *mq, const char *msg);
bool mq_dismiss(struct multiqueue *mq, const char *msg);
int mq_idx(struct multiqueue *mq, GAsyncQueue *q);
GAsyncQueue *mq_get(struct multiqueue *mq, int idx);
void mq_remove_idx(struct multiqueue *mq, int idx);

#endif


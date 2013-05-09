#include "mq.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

void mq_alloc(struct multiqueue *mq, int n) {
	mq->n = n;
	mq->reg = 0;
	mq->open = true;
	mq->queues = malloc(sizeof(GAsyncQueue *) * n);
	pthread_mutex_init(&mq->mutex, NULL);
	pthread_cond_init(&mq->exitcond, NULL);
	memset(mq->queues, 0, sizeof(GAsyncQueue *) * n);
}

GAsyncQueue *mq_register(struct multiqueue *mq) {
	GAsyncQueue *r = NULL;
	pthread_mutex_lock(&mq->mutex);
	if (!mq->open) return NULL;
	for (int i = 0; i < mq->n; ++i) {
		if (mq->queues[i] == NULL) {
			r = mq->queues[i] = g_async_queue_new();
			++mq->reg;
			break;
		}
	}
	pthread_mutex_unlock(&mq->mutex);
	return r;
}

void mq_remove(struct multiqueue *mq, GAsyncQueue *q) {
	pthread_mutex_lock(&mq->mutex);
	for (int i = 0; i < mq->n; ++i) {
		if (mq->queues[i] == q) {
			mq->queues[i] = NULL;
			--mq->reg;
			break;
		}
	}
	if (!mq->open) {
		pthread_cond_signal(&mq->exitcond);
	}
	pthread_mutex_unlock(&mq->mutex);

	void *p;
	while ((p = g_async_queue_try_pop(q)) != NULL) free(p);
	g_async_queue_unref(q);
}

void mq_broadcast(struct multiqueue *mq, const char *msg) {
	if (mq->reg == 0) return;
	pthread_mutex_lock(&mq->mutex);
	for (int i = 0; i < mq->n; ++i) {
		if (mq->queues[i] == NULL) continue;
		g_async_queue_push(mq->queues[i], strdup(msg));
	}
	pthread_mutex_unlock(&mq->mutex);
}

bool mq_dismiss(struct multiqueue *mq, const char *msg) {
	struct timespec tv;

	pthread_mutex_lock(&mq->mutex);
	mq->open = false;
	for (int i = 0; i < mq->n; ++i) {
		if (mq->queues[i] == NULL) continue;
		g_async_queue_push(mq->queues[i], strdup(msg));
	}
	int r = 0;
	tv.tv_sec = time(NULL) + 3;
	tv.tv_nsec = 0;
	while (mq->reg > 0) {
		r = pthread_cond_timedwait(&mq->exitcond, &mq->mutex, &tv);
		if (r == ETIMEDOUT) break;
		r = 0;
	}
	free(mq->queues);
	pthread_mutex_unlock(&mq->mutex);
	return (r != ETIMEDOUT);
}

int mq_idx(struct multiqueue *mq, GAsyncQueue *q) {
	int r = 0xffff;
	pthread_mutex_lock(&mq->mutex);
	for (int i = 0; i < mq->n; ++i) {
		if (mq->queues[i] == q) {
			r = i;
			break;
		}
	}
	pthread_mutex_unlock(&mq->mutex);
	return r;
}

GAsyncQueue *mq_get(struct multiqueue *mq, int idx) {
	if (idx >= mq->n) return NULL;
	pthread_mutex_lock(&mq->mutex);
	GAsyncQueue *r = mq->queues[idx];
	pthread_mutex_unlock(&mq->mutex);
	return r;
}

void mq_remove_idx(struct multiqueue *mq, int idx) {
	pthread_mutex_lock(&mq->mutex);
	GAsyncQueue *q = mq->queues[idx];
	--mq->reg;
	if (!mq->open) {
		pthread_cond_signal(&mq->exitcond);
	}
	pthread_mutex_unlock(&mq->mutex);

	if (q == NULL) return;

	void *p;
	while ((p = g_async_queue_try_pop(q)) != NULL) free(p);
	g_async_queue_unref(q);
}


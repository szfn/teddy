#include "ipc.h"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "global.h"
#include "buffers.h"
#include "interp.h"

char *fusedir = NULL;
char *session_dir = NULL;
struct fuse_chan *fusech = NULL;
static struct fuse *fuse = NULL;

static bool bufferpath(const char *path, int *bid, const char **subpath) {
	*subpath = path + strlen(path);
	*bid = 0;
	for (int i = 1; i < strlen(path); ++i) {
		if ((path[i] == '\0') || (path[i] == '/')) {
			*subpath = path+i;
			break;
		}
		if (!isdigit(path[i])) {
			return false;
		}
		*bid *= 10;
		*bid += (path[i] - '0');
	}

	if (*bid >= buffers_allocated) return false;
	if (buffers[*bid] == NULL) return false;

	return true;
}

static int read_string(char *buf, size_t size, off_t offset, const char *src) {
	size_t len = strlen(src);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, src + offset, size);
	} else
		size = 0;

	return size;
}

struct callback {
	buffer_t *buf;
	char *arg;
	GAsyncQueue *queue;
};

static gboolean do_m_command(struct callback *cb) {
	++(cb->buf->wandercount);
	buffer_move_command(cb->buf, cb->arg, NULL, false);
	--(cb->buf->wandercount);
	editor_t *editor = NULL;
	find_editor_for_buffer(cb->buf, NULL, NULL, &editor);
	if (editor != NULL) {
		gtk_widget_queue_draw(GTK_WIDGET(editor));
	}
	g_async_queue_push(cb->queue, (gpointer)1);
	return FALSE;
}

const char *embedded_move_command(const char *arg, char **pmarg) {
	*pmarg = NULL;
	if (arg[0] != 0x01) return arg;
	if (arg[1] == 0x01) {
		return arg+1;
	}

	int move_end = 1;
	bool ok = false;

	for (;;) {
		if (arg[move_end] == 0x02) {
			ok = true;
			break;
		}
		if (arg[move_end] == 0x00) break;
		++move_end;
	}

	if (!ok) return arg;

	*pmarg = malloc(sizeof(char) * move_end);
	strncpy(*pmarg, arg+1, move_end-1);
	(*pmarg)[move_end-1] = 0x00;

	return arg + move_end+1;
}

static gboolean do_c_command(struct callback *cb) {
	char *marg;
	const char *txt = embedded_move_command(cb->arg, &marg);

	int cursor = -1;
	if (marg != NULL) {
		if (cb->buf->mark < 0) cursor = cb->buf->cursor;
		++(cb->buf->wandercount);
		buffer_move_command(cb->buf, marg, NULL, false);
		--(cb->buf->wandercount);
		free(marg);
	}

	buffer_replace_selection(cb->buf, txt);

	if ((cursor >= 0) && (cursor <= BSIZE(cb->buf))) {
		cb->buf->cursor = cursor;
	}

	editor_t *editor = NULL;
	find_editor_for_buffer(cb->buf, NULL, NULL, &editor);
	if (editor != NULL) {
		gtk_widget_queue_draw(GTK_WIDGET(editor));
	}
	g_async_queue_push(cb->queue, (gpointer)1);

	return FALSE;
}

static gboolean do_exec_command(struct callback *cb) {
	interp_eval(NULL, cb->buf, cb->arg, false, false);
	g_async_queue_push(cb->queue, (gpointer)1);
	return FALSE;
}

static char *buf2str(const char *buf, size_t size) {
	char *r = malloc(sizeof(char) * (size + 1));
	strncpy(r, buf, size);
	r[size] = '\0';
	return r;
}

static int ipc_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	if (strcmp(path, "/cmd") == 0) {
		struct callback cb;
		cb.buf = NULL;
		cb.arg = buf2str(buf, size);
		cb.queue = g_async_queue_new();
		g_idle_add((GSourceFunc)do_exec_command, (gpointer)&cb);
		g_async_queue_pop(cb.queue);
		g_async_queue_unref(cb.queue);
		free(cb.arg);
		return size;
	} else if (strcmp(path, "/event") == 0) {
		return -EACCES;
	}

	// then it must be a buffer
	const char *subpath;
	int bid;
	if (!bufferpath(path, &bid, &subpath)) return -ENOENT;

	if (strcmp(subpath, "/event") == 0) {
		return -EACCES;
	} else if (strcmp(subpath, "/m") == 0) {
		struct callback cb;
		cb.buf = buffers[bid];
		cb.arg = buf2str(buf, size);
		cb.queue = g_async_queue_new();
		g_idle_add((GSourceFunc)do_m_command, (gpointer)&cb);
		g_async_queue_pop(cb.queue);
		g_async_queue_unref(cb.queue);
		free(cb.arg);
		return size;
	} else if (strcmp(subpath, "/c") == 0) {
		struct callback cb;
		cb.buf = buffers[bid];
		cb.arg = buf2str(buf, size);
		cb.queue = g_async_queue_new();
		g_idle_add((GSourceFunc)do_c_command, (gpointer)&cb);
		g_async_queue_pop(cb.queue);
		g_async_queue_unref(cb.queue);
		free(cb.arg);
		return size;
	} else if (strcmp(subpath, "/body") == 0) {
		return -EACCES;
	} else if (strcmp(subpath, "/wd") == 0) {
		return -EACCES;
	} else if (strcmp(subpath, "/name") == 0) {
		return -EACCES;
	} else if (strncmp(subpath, "/prop/", 6) == 0) {
		const char *propname = subpath + 6;
		if (offset != 0) return -EACCES;
		char *v = malloc(sizeof(char) * (size+1));
		strncpy(v,  buf, size);
		v[size] = '\0';
		g_hash_table_insert(buffers[bid]->props, strdup(propname), v);
		return size;
	}

	return -ENOENT;
}

static int do_read_from_multiqueue(struct multiqueue *mq, char *buf, size_t size, struct fuse_file_info *fi) {
	GAsyncQueue *q = NULL;
	if (fi->fh == 0xffff) {
		q = mq_register(mq);
		if (q == NULL) return -EBUSY;
		fi->fh = mq_idx(mq, q);
	} else if (fi->fh == 0xfffe) {
		return -EOF;
	} else {
		q = mq_get(mq, fi->fh);
		if (q == NULL) return -EPIPE;
	}

	void *p = g_async_queue_pop(q);

	if (strcmp(p, "q\n") == 0) {
		mq_remove(mq, q);
		fi->fh = 0xfffe;
	}

	strncpy(buf, p, size);
	int sz = MIN(size, strlen(p));
	free(p);
	return sz;
}

static int ipc_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	if (strcmp(path, "/cmd") == 0) {
		return -EACCES;
	} else if (strcmp(path, "/event") == 0) {
		return do_read_from_multiqueue(&global_event_watchers, buf, size, fi);
	}

	// then it must be a buffer
	const char *subpath;
	int bid;
	if (!bufferpath(path, &bid, &subpath)) return -ENOENT;

	if (strcmp(subpath, "/event") == 0) {
		return do_read_from_multiqueue(&buffers[bid]->watchers, buf, size, fi);
	} else if (strcmp(subpath, "/m") == 0) {
		char *m;
		asprintf(&m, "%d %d", buffers[bid]->mark, buffers[bid]->cursor);
		int r = read_string(buf, size, offset, m);
		free(m);
		return r;
	} else if (strcmp(subpath, "/c") == 0) {
		int start, end;
		buffer_get_selection(buffers[bid], &start, &end);
		if ((start < 0) || (end < 0)) {
			return read_string(buf, size, offset, "");
		} else {
			char *sel = buffer_lines_to_text(buffers[bid], start, end);
			int r = read_string(buf, size, offset, sel);
			free(sel);
			return r;
		}
	} else if (strcmp(subpath, "/body") == 0) {
		int last = size + offset + 1;
		char *b = buffer_lines_to_text(buffers[bid], 0, MAX(BSIZE(buffers[bid]), last));
		int r = read_string(buf, size, offset, b);
		free(b);
		return r;
	} else if (strcmp(subpath, "/wd") == 0) {
		char *bd = buffer_directory(buffers[bid]);
		int r = read_string(buf, size, offset, bd);
		free(bd);
		return r;
	} else if (strcmp(subpath, "/name") == 0) {
		return read_string(buf, size, offset, buffers[bid]->path);
	} else if (strncmp(subpath, "/prop/", 6) == 0) {
		const char *propname = subpath + 6;
		const char *v = g_hash_table_lookup(buffers[bid]->props, propname);
		if (v == NULL) return -ENOENT;
		return read_string(buf, size, offset, v);
	}

	return -ENOENT;
}

static int ipc_getattr_int(const char *path, struct stat *stbuf, bool creating) {
	memset(stbuf, 0, sizeof(struct stat));

	stbuf->st_mode = S_IFREG | 0660;
	stbuf->st_nlink = 1;

	const int rdonly = 0440;
	const int wronly = 0220;
	const int rdwr = 0660;

	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0770;
		stbuf->st_nlink = 2;
		return 0;
	} else if (strcmp(path, "/event") == 0) {
		stbuf->st_mode = S_IFREG | rdonly;
		return 0;
	} else if (strcmp(path, "/cmd") == 0) {
		stbuf->st_mode = S_IFREG | wronly;
		return 0;
	}

	// then it must be a buffer
	const char *subpath;
	int bid;
	if (!bufferpath(path, &bid, &subpath)) return -ENOENT;

	if ((strcmp(subpath, "") == 0) || (strcmp(subpath, "/") == 0)) {
		stbuf->st_mode = S_IFDIR | 0770;
		stbuf->st_nlink = 2;
		return 0;
	} else if (strcmp(subpath, "/event") == 0) {
		stbuf->st_mode = S_IFREG | rdonly;
		return 0;
	} else if (strcmp(subpath, "/m") == 0) {
		stbuf->st_mode = S_IFREG | rdwr;
		return 0;
	} else if (strcmp(subpath, "/c") == 0) {
		stbuf->st_mode = S_IFREG | rdwr;
		return 0;
	} else if (strcmp(subpath, "/body") == 0) {
		stbuf->st_mode = S_IFREG | rdonly;
		return 0;
	} else if (strcmp(subpath, "/wd") == 0) {
		stbuf->st_mode = S_IFREG | rdonly;
		return 0;
	} else if (strcmp(subpath, "/name") == 0) {
		stbuf->st_mode = S_IFREG | rdonly;
		return 0;
	} else if (strcmp(subpath, "/prop") == 0) {
		stbuf->st_mode = S_IFDIR | 0770;
		stbuf->st_nlink = 2;
		return 0;
	} else if (strncmp(subpath, "/prop/", 6) == 0) {
		const char *propname = subpath + 6;
		const char *v = g_hash_table_lookup(buffers[bid]->props, propname);
		if (!creating) {
			if (v == NULL) return -ENOENT;
		}
		stbuf->st_mode = S_IFREG | rdwr;
		return 0;
	}

	return -ENOENT;
}

static int ipc_getattr(const char *path, struct stat *stbuf) {
	return ipc_getattr_int(path, stbuf, false);
}

static int ipc_open_int(const char *path, struct fuse_file_info *fi, bool creating) {
	struct stat stbuf;
	int r = ipc_getattr_int(path, &stbuf, creating);
	if (r < 0) return r;

	switch (fi->flags&3) {
	case O_RDONLY:
		if ((stbuf.st_mode & 0400) == 0) return -EACCES;
		break;
	case O_RDWR:
		if ((stbuf.st_mode & 0400) == 0) return -EACCES;
		if ((stbuf.st_mode & 0200) == 0) return -EACCES;
		break;
	case O_WRONLY:
		if ((stbuf.st_mode & 0200) == 0) return -EACCES;
		break;
	}

	fi->direct_io = 1;
	fi->nonseekable = 1;
	fi->fh = 0xffff;

	return 0;
}

static int ipc_open(const char *path, struct fuse_file_info *fi) {
	return ipc_open_int(path, fi, false);
}

static int ipc_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	int r = ipc_open_int(path, fi, true);
	fi->direct_io = 0;
	return r;
}

static int ipc_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	if (strcmp(path, "/") == 0) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		filler(buf, "event", NULL, 0);
		filler(buf, "cmd", NULL, 0);
#define BIDSZ 20
		char bid[BIDSZ];
		for (int i = 0; i < buffers_allocated; ++i) {
			if (buffers[i] == NULL) continue;
			snprintf(bid, BIDSZ, "%d", i);
			filler(buf, bid, NULL, 0);
		}
		return 0;
	}

	// then it must be a buffer
	const char *subpath;
	int bid;
	if (!bufferpath(path, &bid, &subpath)) return -ENOENT;

	if ((strcmp(subpath, "") == 0) || (strcmp(subpath, "/") == 0)) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		filler(buf, "event", NULL, 0);
		filler(buf, "m", NULL, 0);
		filler(buf, "c", NULL, 0);
		filler(buf, "body", NULL, 0);
		filler(buf, "prop", NULL, 0);
		filler(buf, "wd", NULL, 0);
		filler(buf, "name", NULL, 0);
		return 0;
	} else if (strncmp(subpath, "/prop", 6) == 0) {
		GHashTableIter it;
		g_hash_table_iter_init(&it, buffers[bid]->props);
		gpointer key, value;
		while (g_hash_table_iter_next(&it, &key, &value)) {
			const char *k = (const char *)key;
			filler(buf, k, NULL, 0);
		}
		return 0;
	}

	return -ENOENT;
}

static int ipc_truncate(const char *path, off_t offset) {
	return 0;
}

static int ipc_release(const char *path, struct fuse_file_info *fi) {
	if (fi->fh == 0xffff) return 0;
	if (strcmp(path, "/event") == 0) {
		mq_remove_idx(&global_event_watchers, fi->fh);
	}

	const char *subpath;
	int bid;
	if (!bufferpath(path, &bid, &subpath)) return -ENOENT;

	if (strcmp(subpath, "/event") == 0) {
		mq_remove_idx(&buffers[bid]->watchers, fi->fh);
	}

	return 0;
}

static struct fuse_operations ipc_oper = {
	.getattr = ipc_getattr,
	.readdir = ipc_readdir,
	.read = ipc_read,
	.write = ipc_write,
	.open = ipc_open,
	.create = ipc_create,
	.truncate = ipc_truncate,
	.release = ipc_release,
};

void *fusestart_fn(void *d) {
	fusech = fuse_mount(fusedir, NULL);

	if (fusech == NULL) {
		perror("Could not mount directory");
		exit(EXIT_FAILURE);
	}

	fuse = fuse_new(fusech, NULL, &ipc_oper, sizeof(ipc_oper), NULL);

	if (fuse == NULL) {
		perror("Could not create FUSE structure");
		exit(EXIT_FAILURE);
	}

	int r = fuse_loop_mt(fuse);
	if (r != 0) {
		perror("Could not start FUSE loop");
	}
	return NULL;
}

void ipc_init(void) {
	asprintf(&fusedir, "/tmp/teddy.%d", getpid());
	alloc_assert(fusedir);

	int r = mkdir(fusedir, 0770);
	if (r != 0) {
		perror("Error creating mount directory");
		exit(EXIT_FAILURE);
	}

	pthread_t fusethread;
	pthread_create(&fusethread, NULL, fusestart_fn, NULL);
	pthread_detach(fusethread);
}

static void ipc_rm_link(void) {
	if (session_dir == NULL) return;
	unlink(session_dir);
	free(session_dir);
	session_dir = NULL;
}

void ipc_finalize(void) {
	fuse_exit(fuse);
	fuse_unmount(fusedir, fusech);
	fuse_destroy(fuse);
	int r = rmdir(fusedir);
	if (r != 0) {
		perror("Error unlinking mount directory");
	}
	free(fusedir);
	ipc_rm_link();
}


void ipc_link_to(const char *session_name) {
	ipc_rm_link();
	asprintf(&session_dir, "/tmp/teddy.%s", session_name);
	alloc_assert(session_dir);
	symlink(fusedir, session_dir);
}

void ipc_event(struct multiqueue *mq, buffer_t *buffer, const char *type, const char *detail) {
	char bid[20];
	buffer_to_buffer_id(buffer, bid);
	char *msg;
	asprintf(&msg, "%s %s %s %s\n", type, bid, buffer->path, detail);
	mq_broadcast(mq, msg);
	free(msg);
}

#include "ipc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib.h>

#include "buffers.h"
#include "interp.h"
#include "global.h"

char *event_filename, *cmd_filename;
char *event_session_filename, *cmd_session_filename;
int eventfd = -1, cmdfd = -1;
GIOChannel *event_channel, *cmd_channel;
guint event_source, cmd_source;
bool init_done = false;

#define NCLIENTS 256

int clients[NCLIENTS];

static gboolean event_watch(GIOChannel *source, GIOCondition condition, void *ignored) {
	switch(condition) {
	case G_IO_OUT:
	case G_IO_ERR:
	case G_IO_NVAL:
	case G_IO_HUP:
		quick_message("IPC Error", "Wrong state for event socket\n");
		return FALSE;

	case G_IO_IN:
	case G_IO_PRI: {
		int clientfd = accept(eventfd, NULL, NULL);
		if (clientfd >= 0) {
			for (int i = 0; i < NCLIENTS; ++i) {
				if (clients[i] < 0) {
					clients[i] = clientfd;
					break;
				}
			}
		}
		//printf("New client: %d\n", clientfd);
		break;
	}
	}
	return TRUE;
}

static gboolean cmdclient_read_and_exec(GIOChannel *source) {
#define BUFSIZE 2048
	char buf[BUFSIZE+1];
	int n = 0;
	bool eof = false;


	for (;;) {
		if (eof) {
			g_io_channel_shutdown(source, TRUE, NULL);
			return FALSE;
		}

		gsize m;
		GIOStatus s = g_io_channel_read_chars(source, buf + n, BUFSIZE - n, &m, NULL);
		switch (s) {
		case G_IO_STATUS_ERROR:
		case G_IO_STATUS_EOF:
			eof = true;
			break;
		case G_IO_STATUS_NORMAL:
			break;
		case G_IO_STATUS_AGAIN:
			continue;
		}

		n += m;

		if (n < 3) continue;
		if (buf[n-1] != '\n') continue;
		if (buf[n-2] != '.') continue;
		if (buf[n-3] != '\n') continue;
		break;
	}

	buf[n-3] = '\0';
	//printf("Received: <%s>\n", buf);
	int code = interp_eval(NULL, NULL, buf, false, false);
	char *msg = NULL;

	switch(code) {
	case TCL_OK:
		asprintf(&msg, "%d\n%s\n", code, Tcl_GetStringResult(interp));
		break;
	case TCL_ERROR: {

		Tcl_Obj *options = Tcl_GetReturnOptions(interp, code);
		Tcl_Obj *key = Tcl_NewStringObj("-errorinfo", -1);
		Tcl_Obj *stackTrace;
		Tcl_IncrRefCount(key);
		Tcl_DictObjGet(NULL, options, key, &stackTrace);
		Tcl_DecrRefCount(key);
		asprintf(&msg, "%d\n%s\n", code, Tcl_GetString(stackTrace));
		break;
	}
	case TCL_BREAK:
	case TCL_CONTINUE:
	case TCL_RETURN:
		asprintf(&msg, "%d\n\n", code);
		break;
	}

	Tcl_ResetResult(interp);

	if (msg != NULL) {
		gsize m;
		g_io_channel_write_chars(source, msg, -1, &m, NULL);
		g_io_channel_flush(source, NULL);
		free(msg);
	}

	if (eof) {
		g_io_channel_shutdown(source, TRUE, NULL);
		return FALSE;
	}

	return TRUE;
}

static gboolean cmdclient_watch(GIOChannel *source, GIOCondition condition, void *ignored) {
	switch(condition) {
	case G_IO_OUT:
	case G_IO_ERR:
	case G_IO_NVAL:
		quick_message("IPC Error", "Wrong state for a client socket\n");
	case G_IO_HUP:
		return FALSE;

	case G_IO_IN:
	case G_IO_PRI:
		return cmdclient_read_and_exec(source);
	default: /* what? */
		return FALSE;
	}
}

static gboolean cmd_watch(GIOChannel *source, GIOCondition condition, void *ignored) {
	switch(condition) {
	case G_IO_OUT:
	case G_IO_ERR:
	case G_IO_NVAL:
	case G_IO_HUP:
		quick_message("IPC Error", "Wrong state for command socket\n");
		return FALSE;

	case G_IO_IN:
	case G_IO_PRI: {
		int clientfd = accept(cmdfd, NULL, NULL);
		GIOChannel *cmdclient_channel = g_io_channel_unix_new(clientfd);
		g_io_add_watch(cmdclient_channel, G_IO_IN|G_IO_ERR|G_IO_PRI|G_IO_HUP|G_IO_NVAL, (GIOFunc)cmdclient_watch, NULL);
		break;
	}
	}
	return TRUE;
}

void ipc_init() {
	event_session_filename = cmd_session_filename = NULL;

	struct sockaddr_un event_address, cmd_address;

	bzero(&event_address, sizeof(event_address));
	event_address.sun_family = AF_UNIX;
	asprintf(&event_filename, "/tmp/teddy.%d.event", getpid());
	alloc_assert(event_filename);
	strncpy(event_address.sun_path, event_filename, sizeof(event_address.sun_path) / sizeof(char) - sizeof(char));
	eventfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (eventfd < 0) {
		char *msg;
		asprintf(&msg, "Could not create event socket: %s", strerror(errno));
		alloc_assert(msg);
		quick_message("IPC Error", msg);
		free(msg);
		return;
	}

	if (bind(eventfd, (struct sockaddr *)&event_address, sizeof(event_address)) != 0) {
		char *msg;
		asprintf(&msg, "Could not bind event socket: %s", strerror(errno));
		alloc_assert(msg);
		quick_message("IPC Error", msg);
		free(msg);
		return;
	}

	if (listen(eventfd, 5) != 0) {
		char *msg;
		asprintf(&msg, "Could not listen event socket: %s", strerror(errno));
		alloc_assert(msg);
		quick_message("IPC Error", msg);
		free(msg);
		return;
	}

	bzero(&cmd_address, sizeof(cmd_address));
	cmd_address.sun_family = AF_UNIX;
	asprintf(&cmd_filename, "/tmp/teddy.%d.cmd", getpid());
	alloc_assert(cmd_filename);
	strncpy(cmd_address.sun_path, cmd_filename, sizeof(cmd_address.sun_path) / sizeof(char) - sizeof(char));
	cmdfd  = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cmdfd < 0) {
		char *msg;
		asprintf(&msg, "Could not create command socket: %s", strerror(errno));
		alloc_assert(msg);
		quick_message("IPC Error", msg);
		free(msg);
		return;
	}

	if (bind(cmdfd, (struct sockaddr *)&cmd_address, sizeof(cmd_address)) != 0) {
		char *msg;
		asprintf(&msg, "Could not bind command socket: %s", strerror(errno));
		alloc_assert(msg);
		quick_message("IPC Error", msg);
		free(msg);
		return;
	}

	if (listen(cmdfd, 5) != 0) {
		char *msg;
		asprintf(&msg, "Could not listen command socket: %s", strerror(errno));
		alloc_assert(msg);
		quick_message("IPC Error", msg);
		free(msg);
		return;
	}

	event_channel = g_io_channel_unix_new(eventfd);
	event_source = g_io_add_watch(event_channel, G_IO_IN|G_IO_ERR|G_IO_PRI|G_IO_HUP|G_IO_NVAL, (GIOFunc)event_watch, NULL);
	g_io_channel_set_encoding(event_channel, NULL, NULL);

	cmd_channel = g_io_channel_unix_new(cmdfd);
	cmd_source = g_io_add_watch(cmd_channel, G_IO_IN|G_IO_ERR|G_IO_PRI|G_IO_HUP|G_IO_NVAL, (GIOFunc)cmd_watch, NULL);
	g_io_channel_set_encoding(cmd_channel, NULL, NULL);

	for (int i = 0; i < NCLIENTS; ++i) {
		clients[i] = -1;
	}


	init_done = true;
}

static void ipc_rm_links(void) {
	if (event_session_filename != NULL) {
		unlink(event_session_filename);
		free(event_session_filename);
		event_session_filename = NULL;
	}

	if (cmd_session_filename != NULL) {
		unlink(cmd_session_filename);
		free(cmd_session_filename);
		cmd_session_filename = NULL;
	}
}

void ipc_link_to(const char *session_name) {
	if (!init_done) return;

	ipc_rm_links();

	asprintf(&event_session_filename, "/tmp/teddy.%s.event", session_name);
	alloc_assert(event_session_filename);
	symlink(event_filename, event_session_filename);

	asprintf(&cmd_session_filename, "/tmp/teddy.%s.cmd", session_name);
	alloc_assert(cmd_session_filename);
	symlink(cmd_filename, cmd_session_filename);
}

void ipc_event(buffer_t *buffer, const char *description, const char *arg1) {
	if (!init_done) return;

#define BUFLEN 2048
	char buf[BUFLEN] = "";

	if (buffer != NULL) {
		buffer_to_buffer_id(buffer, buf);
		strcat(buf, " ");
		strcat(buf, buffer->path);
		strcat(buf, " ");
	} else {
		strcat(buf, "@b0 +unnamed ");
	}

	strcat(buf, description);

	if (arg1 != NULL) {
		strcat(buf, " ");
		strcat(buf, arg1);
	}
	strcat(buf, "\n");

	for (int i = 0; i < NCLIENTS; ++i) {
		if (clients[i] < 0) continue;

		const char *p = buf;
		while (strlen(p) > 0) {
			ssize_t m = write(clients[i], p, strlen(p));
			p += m;
		}
	}
}

void ipc_finalize(void) {
	if (!init_done) return;

	for (int i = 0; i < NCLIENTS; ++i) {
		if (clients[i] >= 0) close(clients[i]);
	}

	g_source_remove(event_source);
	g_source_remove(cmd_source);

	g_io_channel_shutdown(event_channel, TRUE, NULL);
	g_io_channel_shutdown(cmd_channel, TRUE, NULL);

	unlink(event_filename);
	unlink(cmd_filename);

	ipc_rm_links();
}

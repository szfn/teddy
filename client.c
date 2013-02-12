#include "client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "global.h"

#define BUFIDSIZE 20

int eventfd, cmdfd;
pthread_t eventhread;

pthread_mutex_t bufid_check_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t bufid_check_cond = PTHREAD_COND_INITIALIZER;
bool bufid_bufferclose_found = false;
char bufid_check[BUFIDSIZE];

static int readline(int fd, char *buf, size_t s) {
	for (int i = 0; i < s-1; ++i) {
		char c;
		ssize_t r = read(fd, &c, 1);
		if (r <= 0) return r;
		if (c == '\n') {
			buf[i] = 0;
			return 0;
		}
		buf[i] = c;
	}
	buf[s-1] = 0;
	return -1;
}

static void *eventhread_fn(void *d) {
#define EVENTSIZE 2048
	char line[EVENTSIZE];

	for (;;) {
		if (readline(eventfd, line, EVENTSIZE) < 0) break;
		//printf("event: <%s>\n", line);

		char *tok;
		char *bufid = strtok_r(line, " ", &tok);
		/* char *bufpath = */ strtok_r(NULL, " ", &tok);
		char *type = strtok_r(NULL, " ", &tok);

		if (strcmp(type, "bufferclose") != 0) continue;

		pthread_mutex_lock(&bufid_check_mutex);

		if (strcmp(bufid, bufid_check) == 0) {
			strcpy(bufid_check, "none");
			bufid_bufferclose_found = true;
			pthread_cond_signal(&bufid_check_cond);
		}

		pthread_mutex_unlock(&bufid_check_mutex);
	}

	return NULL;
}

bool tepid_check(void) {
	const char *tepid = getenv("TEPID");

	if (tepid == NULL) return false;
	if (strcmp(tepid, "") == 0) return false;

	char *event_path, *cmd_path;
	asprintf(&event_path, "/tmp/teddy.%s.event", tepid);
	alloc_assert(event_path);
	asprintf(&cmd_path, "/tmp/teddy.%s.cmd", tepid);
	alloc_assert(cmd_path);

	bool r = false;

	if (access(event_path, F_OK) != 0) goto cleanup_alloc;
	if (access(cmd_path, F_OK) != 0) goto cleanup_alloc;

	eventfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (eventfd < 0) goto cleanup_alloc;

	cmdfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cmdfd < 0) goto cleanup_alloc;

	struct sockaddr_un event_address, cmd_address;
	bzero(&event_address, sizeof(event_address));
	bzero(&cmd_address, sizeof(cmd_address));

	event_address.sun_family = AF_UNIX;
	cmd_address.sun_family = AF_UNIX;
	strncpy(event_address.sun_path, event_path, sizeof(event_address.sun_path) / sizeof(char));
	strncpy(cmd_address.sun_path, cmd_path, sizeof(cmd_address.sun_path) / sizeof(char));

	if (connect(eventfd, &event_address, sizeof(event_address)) != 0) {
		perror("Connect to event socket");
		goto cleanup_socks;
	}
	if (connect(cmdfd, &cmd_address, sizeof(cmd_address)) != 0) {
		perror("Connect to command socket");
		goto cleanup_socks;
	}

	strcpy(bufid_check, "none");
	pthread_create(&eventhread, NULL, eventhread_fn, NULL);

	r = true;

cleanup_socks:
	if (!r) {
		close(eventfd);
		close(cmdfd);
	}

cleanup_alloc:
	free(event_path);
	free(cmd_path);

	return r;
}

void writeall(int fd, const char *msg) {
	const char *p = msg;
	while (*p != '\0') {
		ssize_t m = write(fd, p, strlen(p) * sizeof(char));
		if (m < 0) {
			if (errno != EAGAIN) break;
		}
		p += m;
	}
}

static int client_main1(const char *a) {
	writeall(cmdfd, "O ");
	char *wd = get_current_dir_name();
	writeall(cmdfd, wd);
	free(wd);
	writeall(cmdfd, "/");
	writeall(cmdfd, a);
	writeall(cmdfd, "\n.\n");

	char bufid[BUFIDSIZE];

	// reads and checks response code
	if (readline(cmdfd, bufid, BUFIDSIZE) < 0) return -1;
	if (strlen(bufid) <= 0) return -1;
	if (bufid[0] != '0') return -1;

	if (readline(cmdfd, bufid, BUFIDSIZE) < 0) return -1;

	pthread_mutex_lock(&bufid_check_mutex);
	strcpy(bufid_check, bufid);
	bufid_bufferclose_found = false;
	pthread_mutex_unlock(&bufid_check_mutex);

	pthread_mutex_lock(&bufid_check_mutex);
	while (!bufid_bufferclose_found) {
		pthread_cond_wait(&bufid_check_cond, &bufid_check_mutex);
	}
	pthread_mutex_unlock(&bufid_check_mutex);

	return 0;
}

int client_main(int argc, char *argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (client_main1(argv[i]) < 0) break;
	}

	close(eventfd);
	close(cmdfd);

	//pthread_join(eventhread, NULL);

	return 0;
}


#include "client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "global.h"

#define BUFIDSIZE 20

int client_argc;

static void *eventhread_fn(void *event_path) {
	int eventfd = open((const char *)event_path, O_RDONLY);
#define EVENTSIZE 2048
	char line[EVENTSIZE];

	int toopen = client_argc-1;
	int *bufids = malloc(sizeof(int) * (client_argc-1));
	for (int i = 0; i < client_argc-1; ++i) bufids[i] = -1;
	int toclose = 0;

	for (;;) {
		ssize_t r = read(eventfd, line, EVENTSIZE);
		if (r < 0) break;
		line[r] = '\0';

		char *tok;
		char *type = strtok_r(line, " ", &tok);
		char *bufid_str = strtok_r(NULL, " ", &tok);
		char *bufferpath = strtok_r(NULL, " ", &tok);
		// char *detail = srttok_r(NULL, " ", &tok);

		if (bufid_str[0] != '@') continue;
		if (bufid_str[1] != 'b') continue;

		int bufid = atoi(bufid_str+2);

		if ((strcmp(type, "new") == 0) && (bufferpath[0] != '+')) {
			if (toopen > 0) {
				--toopen;
				++toclose;
				bufids[toopen] = bufid;
			}
		} else if (strcmp(type, "bufferclose") == 0) {
			for (int i = 0; i < client_argc-1; ++i) {
				if (bufids[i] == bufid) {
					bufids[i] = -1;
					--toclose;
					break;
				}
			}
		}

		if ((toopen == 0) && (toclose == 0)) {
			break;
		}
	}

	free(bufids);
	close(eventfd);
	free(event_path);

	return NULL;
}

bool tepid_check(void) {
	const char *tepid = getenv("TEPID");

	if (tepid == NULL) return false;
	if (strcmp(tepid, "") == 0) return false;

	char *event_path;
	asprintf(&event_path, "/tmp/teddy.%s/event", tepid);
	alloc_assert(event_path);

	if (access(event_path, F_OK) != 0) {
		printf("Access error to %s\n", event_path);
		free(event_path);
		return false;
	}

	return true;
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

int client_main(int argc, char *argv[]) {
	char *cmd_path, *event_path;
	asprintf(&cmd_path, "/tmp/teddy.%s/cmd", getenv("TEPID"));
	alloc_assert(cmd_path);
	asprintf(&event_path, "/tmp/teddy.%s/event", getenv("TEPID"));
	alloc_assert(event_path);

	client_argc = argc;

	pthread_t event_thread;
	pthread_create(&event_thread, NULL, eventhread_fn, event_path);
	sleep(1);

	FILE *fcmd = fopen(cmd_path, "w");
	if (fcmd == NULL) {
		perror("Couldn't open command file");
		exit(EXIT_FAILURE);
	}

	for (int i = 1; i < argc; ++i) {
		fprintf(fcmd, "O %s\n", argv[i]);
	}

	fclose(fcmd);
	free(cmd_path);

	pthread_join(event_thread, NULL);

	return 0;
}


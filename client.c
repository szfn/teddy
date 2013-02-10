#include "client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "global.h"

int eventfd, cmdfd;
pthread_t eventhread;

static void *eventhread_fn(void *d) {
	//TODO:
	// - read eventfd
	// - check if it's the event we are waiting for
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

	strncpy(event_address.sun_path, event_path, sizeof(event_address.sun_path) / sizeof(char));
	strncpy(cmd_address.sun_path, cmd_path, sizeof(cmd_address.sun_path) / sizeof(char));

	if (connect(eventfd, &event_address, sizeof(event_address)) != 0) goto cleanup_socks;
	if (connect(cmdfd, &cmd_address, sizeof(cmd_address)) != 0) goto cleanup_socks;

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

void client_main1(const char *a) {
	writeall(cmdfd, "O ");
	//TODO: resolve relative path
	writeall(cmdfd, a);
	writeall(cmdfd, "\n.\n");

	//TODO:
	// - read response
	// - wait bufferclose on eventfd
	// - close both and return
}

int client_main(int argc, char *argv[]) {
	for (int i = 1; i < argc; ++i) {
		client_main1(argv[1]);
	}

	close(eventfd);
	close(cmdfd);

	return 0;
}


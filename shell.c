#include "shell.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

int teddy_fdopen_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int i, intr;
	int flags = 0;
	Tcl_Obj *r;
	
	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments ot 'fdopen' command");
		return TCL_ERROR;
	}

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "-append") == 0) {
			flags |= O_APPEND;
		} else if (strcmp(argv[i], "-async") == 0) {
			flags |= O_ASYNC;
		} else if (strcmp(argv[i], "-cloexec") == 0) {
			flags |= O_CLOEXEC;
		} else if (strcmp(argv[i], "-creat") == 0) {
			flags |= O_CREAT;
		} else if (strcmp(argv[i], "-direct") == 0) {
			flags |= O_DIRECT;
		} else if (strcmp(argv[i], "-directory") == 0) {
			flags |= O_DIRECTORY;
		} else if (strcmp(argv[i], "-excl") == 0) {
			flags |= O_EXCL;
		} else if (strcmp(argv[i], "-largefile") == 0) {
			flags |= O_LARGEFILE;
		} else if (strcmp(argv[i], "-noatime") == 0) {
			flags |= O_NOATIME;
		} else if (strcmp(argv[i], "-noctty") == 0) {
			flags |= O_NOCTTY;
		} else if (strcmp(argv[i], "-nofollow") == 0) {
			flags |= O_NOFOLLOW;
		} else if (strcmp(argv[i], "-nonblock") == 0) {
			flags |= O_NONBLOCK;
		} else if (strcmp(argv[i], "-sync") == 0) {
			flags |= O_SYNC;
		} else if (strcmp(argv[i], "-trunc") == 0) {
			flags |= O_TRUNC;
		} else if (strcmp(argv[i], "-rdonly") == 0) {
			flags |= O_RDONLY;
		} else if (strcmp(argv[i], "-wronly") == 0) {
			flags |= O_WRONLY;
		} else if (strcmp(argv[i], "-rdwr") == 0) {
			flags |= O_RDWR;
		} else {
			Tcl_AddErrorInfo(interp, "Unknown option passed to 'fdopen' command");
			return TCL_ERROR;
		}
	}

	if (i != argc - 1) {
		Tcl_AddErrorInfo(interp, "Extraneous arguments given to 'fdopen' command");
		return TCL_ERROR;
	}

	intr = open(argv[i], flags);
	r = Tcl_NewIntObj(intr);
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing fdopen");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, r);
	
	return TCL_OK;
}

int teddy_fdclose_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_Obj *r;
	int intr;

	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'fdclose' command");
		return TCL_ERROR;
	}

	intr = close(atoi(argv[1]));
	r = Tcl_NewIntObj(intr);
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing fdclose");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

int teddy_fddup2_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_Obj *r;
	int intr;

	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'fddup2' command");
		return TCL_ERROR;
	}

	intr = dup2(atoi(argv[1]), atoi(argv[2]));
	r = Tcl_NewIntObj(intr);
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing fddup2");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

int teddy_fdpipe_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int pipefd[2];
	int ret;
	Tcl_Obj *pipeobj[2], *retlist;

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'fdpipe' command");
		return TCL_ERROR;
	}

	ret = pipe(pipefd);

	if (ret < 0) {
		Tcl_AddErrorInfo(interp, "Error executing fdpipe command");
		return TCL_ERROR;
	}

	pipeobj[0] = Tcl_NewIntObj(pipefd[0]);
	Tcl_IncrRefCount(pipeobj[0]);
	pipeobj[1] = Tcl_NewIntObj(pipefd[1]);
	Tcl_IncrRefCount(pipeobj[1]);

	retlist = Tcl_NewListObj(2, pipeobj);
	Tcl_DecrRefCount(pipeobj[0]);
	Tcl_DecrRefCount(pipeobj[1]);

	Tcl_SetObjResult(interp, retlist);

	return TCL_OK;
}

int teddy_posixfork_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_Obj *r;
	int intr;

	if (argc != 1) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to posixfork command");
		return TCL_ERROR;
	}

	intr = fork();
	r = Tcl_NewIntObj(intr);
	if (intr == -1) {
		Tcl_AddErrorInfo(interp, "Error executing posixfork command");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, r);

	return TCL_OK;
}

int teddy_posixexec_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	// first argument passed to this command is the name of this command (i.e. the string "posixexec") this needs to be discarded before calling execvp and a NULL needs to be appended to argv as a terminator

	int i;
	char ** newargv = malloc(sizeof(char *) * argc);

	for (i = 1; i < argc; ++i) {
		newargv[i-1] = (char *)argv[i];
	}
	newargv[argc-1] = NULL;

	if (execvp(newargv[0], newargv) == -1) {
		Tcl_AddErrorInfo(interp, "Error executing posixexec command");
		return TCL_ERROR;
	}

	return TCL_OK;
}

int teddy_posixwaitpid_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	int i;
	int status, options = 0;
	pid_t r;
	pid_t pid;
	Tcl_Obj *retval[2], *retlist;

	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments ot 'posixwaitpid' command");
		return TCL_ERROR;
	}

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-') break;
		if (strcmp(argv[i], "-wexited") == 0) {
			options |= WEXITED;
		} else if (strcmp(argv[i], "-wstopped") == 0) {
			options |= WSTOPPED;
		} else if (strcmp(argv[i], "-wcontinued") == 0) {
			options |= WCONTINUED;
		} else if (strcmp(argv[i], "-wnohang") == 0) {
			options |= WNOHANG;
		} else if (strcmp(argv[i], "-wnowait") == 0) {
			options |= WNOWAIT;
		} else {
			Tcl_AddErrorInfo(interp, "Unknown option passed to 'posixwaitpid' command");
			return TCL_ERROR;
		}
	}

	if (i != argc - 1) {
		Tcl_AddErrorInfo(interp, "Extraneous arguments given to 'posixwaitpid' command");
		return TCL_ERROR;
	}

	pid = atoi(argv[i]);

	r = waitpid(pid, &status, options);

	if (r == -1) {
		Tcl_AddErrorInfo(interp, "Error executing waitpid command");
		return TCL_ERROR;
	}

	//printf("WAITPID output: %d %d\n", r, status);

	retval[0] = Tcl_NewIntObj(r);
	Tcl_IncrRefCount(retval[0]);
	retval[1] = Tcl_NewIntObj(WEXITSTATUS(status));
	Tcl_IncrRefCount(retval[1]);

	retlist = Tcl_NewListObj(2, retval);
	Tcl_SetObjResult(interp, retlist);
	Tcl_DecrRefCount(retval[0]);
	Tcl_DecrRefCount(retval[1]);

	return TCL_OK;
}

int teddy_posixexit_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments ot 'posixwaitpid' command");
		return TCL_ERROR;
	}

	exit(atoi(argv[1]));

	return TCL_OK;
}

int teddy_fd2channel_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "fd2channel command can not be called without an argument");
		return TCL_ERROR;
	}

	int fd = atoi(argv[1]);
	const char *type = argv[2];

	if (fd < 0) {
		Tcl_AddErrorInfo(interp, "Wrong argument to fd2channel (not a file descriptor)");
		return TCL_ERROR;
	}

	int readOrWrite;

	if (strcmp(type, "read") == 0) {
		readOrWrite = TCL_READABLE;
	} else if (strcmp(type, "write") == 0) {
		readOrWrite = TCL_WRITABLE;
	} else {
		Tcl_AddErrorInfo(interp, "Wrong argument to fd2channel (not read or write)");
	}

	Tcl_Channel chan = Tcl_MakeFileChannel(fd, readOrWrite);
	Tcl_RegisterChannel(interp, chan);
	Tcl_SetResult(interp, Tcl_GetChannelName(chan), NULL);

	return TCL_OK;
}

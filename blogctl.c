/*
 * blogd.c
 *
 * Copyright 2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "libconsole.h"

/*
 * Cry and exit.
 */
void error (const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verr(EXIT_FAILURE, fmt, ap);
    va_end(ap);
}

static int getsocket(void)
{
    int fd;
    set_signal(SIGPIPE, NULL, SIG_IGN);
    fd = open_un_socket_and_connect();
    return fd;
}

static char getcmd(int argc, char *argv[])
{
    static const struct {
	const char* cmd;
	const char req;
	const int arg;
	const char* opt;
    } cmds[] = {
	{ "root=",		MAGIC_CHROOT,		1, NULL	},	/* New root */
	{ "ping",		MAGIC_PING,		0, NULL	},	/* Ping */
	{ "ready",		MAGIC_SYS_INIT,		0, NULL	},	/* System ready */
	{ "quit",		MAGIC_QUIT,		0, NULL	},	/* Quit */
	{ "final",		MAGIC_FINAL,		0, NULL	},	/* Final */
	{ "close",		MAGIC_CLOSE,		0, NULL	},	/* Close logging only */
	{ "deactivate",		MAGIC_DEACTIVATE,	0, NULL	},	/* Deactivate logging */
	{ "reactivate",		MAGIC_REACTIVATE,	0, NULL	},	/* Reactivate logging */
	{}
    }, *cmd = cmds;
    char ret = (char)-1;

    if (argc <= 1)
	goto out;

    optarg = NULL;
    if (argv[optind] && *argv[optind]) {
	int n = optind++;
	for (; cmd->cmd; cmd++)
	    if (cmd->arg) {
		if (strncmp(cmd->cmd, argv[n], strlen(cmd->cmd)) == 0) {
		    optarg = strchr(argv[n], '=');
		    optarg++;
		    ret = cmd->req;
		    break;
		}
	    } else {
		if (strcmp(cmd->cmd, argv[n]) == 0) {
		    ret = cmd->req;
		    break;
		}
	    }
    }
out:
    return ret;
}

int main(int argc, char *argv[])
{
    char *root = NULL;
    char *message, answer[2], cmd[2];
    int fdsock = -1, ret, len;

    cmd[1] = '\0';
    answer[0] = '\x15';

    fdsock = getsocket();
    if (fdsock < 0)
	error("no blogd active");

    while ((cmd[0] = getcmd(argc, argv)) != (char)-1) {
        switch (cmd[0]) {
	case MAGIC_CHROOT:
	    root = optarg;
	    len = (int)strlen(root);
	    if (len > UCHAR_MAX || len < 1) {
		errno = EINVAL;
		error("can not send message");
	    }
	    message = NULL;
	    ret = asprintf(&message, "%c\002%c%s%n", cmd[0], (int)(strlen(root) + 1), root, &len);
	    if (ret < 0)
		error("can not allocate message");
	    safeout(fdsock, message, len+1, SSIZE_MAX);
	    free(message);
	    break;
	case MAGIC_PING:
	case MAGIC_SYS_INIT:
	case MAGIC_QUIT:
	case MAGIC_FINAL:
	case MAGIC_CLOSE:
	case MAGIC_DEACTIVATE:
	case MAGIC_REACTIVATE:
	    safeout(fdsock, cmd, strlen(cmd)+1, SSIZE_MAX);
	    break;
	case '?':
	default:
	    goto fail;
        }

	if (can_read(fdsock, 1000)) {
	    answer[0] = '\0';
	    safein(fdsock, &answer[0], sizeof(answer));
	}

	break;		/* One command per call only */
    }

    argv += optind;
    argc -= optind;
fail:
    if (fdsock >= 0)
	close(fdsock);

    return answer[0] == '\x6' ? 0 : 1;
}

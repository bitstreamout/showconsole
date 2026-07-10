/*
 * blogctl.c
 *
 * Copyright 2015, 2026 Werner Fink, 2015 SuSE Linux GmbH,
 * 2026 SUSE Software Solutions Germany GmbH
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <endian.h>	/* For le32toh */
#include <err.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include "libconsole.h"

/*
 * Cry and exit.
 */
__attribute__((noreturn)) void error (const char *fmt, ...)
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

#define MAGIC_HELP	0x19
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
	{ "ask-for-password",	MAGIC_ASK_PWD,		0, NULL	},	/* Ask for password */
 	{ "ask-question",	MAGIC_QUESTION,		0, NULL	},	/* Ask a question */
	{ "display-message",	MAGIC_SHOW_MSG,		0, NULL	},	/* Display a message */
	{ "hide-message",	MAGIC_HIDE_MSG,		0, NULL	},	/* Sorry, no backroll */
	{ "ready",		MAGIC_SYS_INIT,		0, NULL	},	/* System ready */
	{ "quit",		MAGIC_QUIT,		0, NULL	},	/* Quit */
	{ "final",		MAGIC_FINAL,		0, NULL	},	/* Final */
	{ "close",		MAGIC_CLOSE,		0, NULL	},	/* Close logging only */
	{ "deactivate",		MAGIC_DEACTIVATE,	0, NULL	},	/* Deactivate logging */
	{ "reactivate",		MAGIC_REACTIVATE,	0, NULL	},	/* Reactivate logging */
	{ "help",		MAGIC_HELP,		0, NULL	},	/* End Of Medium aka Help */
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

static int wait_for_blogd_close(int fd)
{
     struct pollfd pfd;
     pfd.fd = fd;
     pfd.events = POLLIN|POLLRDHUP;
     pfd.revents = 0;
 
     while (1) {
	 int ret = poll(&pfd, 1, -1);
 
	 if (ret < 0) {
	     if (errno == EINTR)
		 continue;
	     return -1;
	 }
 
	 if (pfd.revents & (POLLHUP|POLLRDHUP|POLLIN)) {
	     char buf[1];
	     /* Verify if socket is really down (EOF = 0 bytes) */
	     int n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
	     if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
		 return 0;       /* down */
	 }
     }
}

int main(int argc, char *argv[])
{
    char *root = NULL;
    char *message, answer[2], cmd[2];
    int fdsock = -1, ret, len, do_wait = 0;

    cmd[1] = '\0';
    answer[0] = '\x15';

    while ((cmd[0] = getcmd(argc, argv)) != (char)-1) {
	if (cmd[0] != MAGIC_HELP && fdsock < 0) {
	    fdsock = getsocket();
	    if (fdsock < 0)
		error("no blogd active");
	}
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
 	    /* Check if --wait was passed as an argument to quit */
 	    if (cmd[0] == MAGIC_QUIT && argv[optind] && strcmp(argv[optind], "--wait") == 0) {
 		do_wait = 1;
 		optind++; /* Consume the argument so getcmd doesn't trip over it */
 	    }
	case MAGIC_FINAL:
	case MAGIC_CLOSE:
	case MAGIC_DEACTIVATE:
	case MAGIC_REACTIVATE:
	    safeout(fdsock, cmd, strlen(cmd)+1, SSIZE_MAX);
	    break;
	case MAGIC_ASK_PWD:
	case MAGIC_QUESTION: {
	    char *prompt = NULL;
	    char *command = NULL;
	    int c, tries = 1;

	    /* Parse the parameters */
	    static struct option long_options[] = {
		{"prompt",		required_argument, 0, 'p'},
		{"command",		required_argument, 0, 'c'},
		{"number-of-tries",	required_argument, 0, 'n'},
		{"dont-pause-progress",	no_argument,	   0, 'd'},
		{0, 0, 0, 0}
	    };

	    /* 
	     * The getopt_long_only function parses starting from argv[optind].
	     * Since we used getcmd(), optind is pointing exactly at the first option.
	     */
	    while ((c = getopt_long_only(argc, argv, "", long_options, NULL)) != -1) {
		switch (c) {
		    case 'p':
			prompt = optarg;
			break;
		    case 'c':
			command = optarg;
			break;
		    case 'n':
			tries = atoi(optarg);
			break;
		    case 'd':
			/* Ignored by blogd as it is useless  */
			break;
		    case '?':
		    default:
			break;
		}
	    }

	    if (!prompt)
		prompt = (cmd[0] == MAGIC_ASK_PWD) ? "Password" : "Question";

	    len = (int)strlen(prompt);
	    if (len >= UCHAR_MAX) {
		errno = EINVAL;
		error("prompt string too long (max 254 chars)");
	    }

	    do {
		message = NULL;
		ret = asprintf(&message, "%c\002%c%s%n", cmd[0], len + 1, prompt, &len);
		if (ret < 0)
		    error("can not allocate message");
		
		safeout(fdsock, message, len + 1, SSIZE_MAX);
		free(message);

		/* Wait for the users answer */
		if (can_read(fdsock, -1)) {
		    char ans;
		    safein(fdsock, &ans, 1);

		    if (ans == '\t') {			/* ANSWER_MLT */
			uint32_t plen;
			safein(fdsock, &plen, sizeof(plen));
			plen = le32toh(plen);	/* Plymouth sends le32 */

			char *pwd = calloc(1, plen + 1);
			if (!pwd)
			    error("memory allocation failed");
			    
			safein(fdsock, pwd, plen);

			if (command) {
			    FILE *p = popen(command, "w");
			    if (p) {
				fputs(pwd, p);
				if (pclose(p) == 0) {
				    free(pwd);
				    answer[0] = '\x6';	/* Fake ACK for success */
				    goto end_cmd;
				}
			    }
			} else {
			    printf("%s\n", pwd);
			    free(pwd);
			    answer[0] = '\x6';		/* Fake ACK for success */
			    goto end_cmd;
			}
			free(pwd);
		    } else if (ans == '\x5') { 
			/* ANSWER_ENQ: Cancel / No input */
			break;
		    }
		}
		tries--;
	    } while (tries > 0);

	    /* If we end up here, all attempts have failed */
	    answer[0] = '\x15';				/* NACK */
	    goto end_cmd;
	}
	case MAGIC_SHOW_MSG:
	case MAGIC_HIDE_MSG: {
	    char *text = "";
	    int c;

	    static struct option long_options[] = {
		{"text", required_argument, 0, 't'},
		{0, 0, 0, 0}
	    };

	    while ((c = getopt_long_only(argc, argv, "", long_options, NULL)) != -1) {
		if (c == 't')
		    text = optarg;
	    }

	    len = (int)strlen(text);
	    if (len >= UCHAR_MAX) {
		errno = EINVAL;
		error("message string too long (max 254 chars)");
	    }

	    message = NULL;
	    ret = asprintf(&message, "%c\002%c%s%n", cmd[0], len + 1, text, &len);
	    if (ret < 0)
		error("can not allocate message");
	    
	    safeout(fdsock, message, len + 1, SSIZE_MAX);
	    free(message);
	    
	    break;
	}
	case MAGIC_HELP:
	    printf("Usage: /sbin/blogctl [COMMAND] [OPTIONS]\n\n"
		   "Commands:\n"
		   "  ping                  Check if blogd is active\n"
		   "  quit [--wait]         Gracefully terminate blogd\n"
		   "  root=<path>           Set new root file system path\n"
		   "  ready                 Signal that file systems are writable\n"
		   "  close                 Finish logging, allow systemd to unmount\n"
		   "  ask-for-password      Ask the user for a password\n"
		   "    --prompt=STRING       Message to display\n"
		   "    --command=STRING      Command to receive password via stdin\n"
		   "    --number-of-tries=INT Number of retries\n"
		   "  ask-question          Ask the user a question\n"
		   "    --prompt=STRING       Message to display\n"
		   "    --command=STRING      Command to receive answer via stdin\n"
		   "  display-message       Display a status message\n"
		   "    --text=STRING         The message text\n"
		   "  hide-message          Hide a status message\n"
		   "  deactivate            Disconnect blogd from system console\n"
		   "  reactivate            Reconnect blogd to system console\n"
		   "  final                 Rotate boot.log to boot.old\n"
		   "  help                  Show this help text\n");
	    answer[0] = '\x6';
	    goto fail;
	case '?':
	default:
	    goto fail;
	}

	if (can_read(fdsock, 1000)) {
	    answer[0] = '\0';
	    safein(fdsock, &answer[0], sizeof(answer));
	}
end_cmd:
	/* If we received an ACK (0x06) and --wait was requested, block until closed */
	if (do_wait && answer[0] == '\x6')
	    wait_for_blogd_close(fdsock);

	break;		/* One command per call only */
    }

    argv += optind;
    argc -= optind;
    if (argc != 0)
	printf("Usage: /sbin/blogctl help\n");
fail:
    if (fdsock >= 0)
	close(fdsock);

    return answer[0] == '\x6' ? 0 : 1;
}

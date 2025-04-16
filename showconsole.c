/*
 * showconsole.c
 *
 * Copyright 2000,2015 Werner Fink, 2000,2015 SuSE GmbH Nuernberg, Germany.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <sys/types.h> /* Defines the macros major and minor */
#include <sys/sysmacros.h> /* Defines the macros major and minor */
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include "libconsole.h"

/*
 * Internal logger
 */
static char *myname = NULL;

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

/*
 * Now do the job
 */
int main(int argc, char *argv[])
{
    const char* opt = argv[1];
    struct console *c;
    char numeric = 0;
    myname = program_invocation_short_name;

    if (!strcmp(myname, "setconsole")) {
	char *curtty = ttyname(0);
	int fd, isconsole = 0, c = 0, flags;

	if (curtty && (isconsole = (strcmp("/dev/console", curtty) == 0)))
	    fd = 0;
	else do {
	    if (c++ > 20)
	    	error("can not open console: %m");
	    fd = open("/dev/console", O_RDWR|O_NOCTTY|O_NONBLOCK);
	    if (fd >= 0)
		break;
	    if (errno != EIO)
	    	error("can not open console: %m");
	    usleep(50000);
	} while (errno == EIO);

	flags = fcntl(fd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);

	(void)ioctl(fd, TIOCCONS, NULL);        /* Undo any current map if any */
	close(fd);

	if (argc < 2)
	    goto out;

	if (argc > 2)
	    error("Usage: %s [-r | <tty> ]", myname);

	if (opt && *opt++ == '-' && *opt++ == 'r' && *opt == '\0')
	    goto out;

	if ((fd = open(argv[1], O_WRONLY|O_NOCTTY)) < 0)
	    error("can not open %s: %m", argv[1]);

	if (!isatty(fd))
	    error("%s is not a tty", argv[1]);

	if (ioctl(fd, TIOCCONS, NULL) < 0)
	    error("can not set console device: %m");

	goto out;
    }

    if (argc == 2) {
	if (opt && *opt++ == '-' && *opt++ == 'n' && *opt == '\0')
	    numeric++;
	else
	    error("Usage: %s [-n]", myname);
    } else if (argc > 2)
	error("Usage: %s [-n]", myname);

    getconsoles(&cons, 0);
    list_for_each_entry(c, &cons->node, node) {
       	if (c->flags & CON_CONSDEV) {
	    if (numeric)
		printf("%u %u\n", major(c->dev), minor(c->dev));
	    else
		printf("%s\n", c->tty);
	    numeric = 2;
	    break;
	}
    }
    if (numeric != 2)
	error("real console unknown");
out:
    return 0;
}

/*
 * tty.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "libconsole.h"

int open_tty(const char *name, int flags)
{
    int ret, fd;

    ret = 0;
    do {
	if (ret++ > 20)
	    return -1;

	fd = open(name, flags);
	if (fd >= 0)
	    break;
	if (errno != EIO)
	    return -1;
	usleep(50000);

    } while (errno == EIO);

    ret = isatty(fd);
    if (!ret) {
	close(fd);
	errno = ENOTTY;
	return -1;
    }

    return fd;
}

int request_tty(const char *tty)
{
    struct sigaction saved_sighup;
    int fd = -1, nd, wd;
#if defined(__s390__) || defined(__s390x__)
    struct stat sb;
#endif

    fd = open("/dev/tty", O_RDWR|O_NOCTTY|O_CLOEXEC|O_NONBLOCK);
    if (fd >= 0) {
	set_signal(SIGHUP, &saved_sighup, SIG_IGN);
	(void)ioctl(fd, TIOCNOTTY);
	reset_signal(SIGHUP, &saved_sighup);
	close(fd);
    }

    nd = inotify_init1(IN_CLOEXEC);
    if (nd < 0) {
	warn("can not initialize monitoring %s", tty);
	return -1;
    }

    wd = inotify_add_watch(nd, tty, IN_CLOSE);
    if (wd < 0) {
	warn("can not add a watch on inotifier %d for %s", nd, tty);
	return -1;
    }

    do {
	ssize_t len;
	int ret, flags;

	clear_input(nd);

	fd = open_tty(tty, O_RDWR|O_NOCTTY|O_CLOEXEC|O_NONBLOCK);
	if (fd < 0) {
	    warn("can not open %s", tty);
	    break;
	}

	set_signal(SIGHUP, NULL, SIG_IGN);
	ret = ioctl(fd, TIOCSCTTY, 0);
	reset_signal(SIGHUP, &saved_sighup);

	if (ret < 0 && errno != EPERM) {
	    tcdrain(fd);
	    close(fd);
	    break;
	}

#if defined(__s390__) || defined(__s390x__)
	if (fstat(fd, &sb) == 0 && major(sb.st_dev) == 4 && minor(sb.st_dev) == 64)
	    goto noblock;
#endif
	flags = fcntl(fd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
#if defined(__s390__) || defined(__s390x__)
    noblock:
#endif

	if (ret >= 0)
	    break;	/* Success */

	do {
# define BUF_LEN    ((sizeof(struct inotify_event)+NAME_MAX+1))
	    unsigned char buf[BUF_LEN];
	    ssize_t e;

	    if (!can_read(nd, -1))
		break;

	    len = read (nd, buf, BUF_LEN);
	    if (len < 0) {
		if (errno == EINTR || errno == EAGAIN)
		    continue;
		goto out;
	    }

	    e = 0;
	    while (e < len) {
		struct inotify_event *ev;

		ev = (struct inotify_event *)&buf[e];
		if (ev->wd == wd && (ev->mask&IN_CLOSE))
		    break;

		e += sizeof(struct inotify_event) + ev->len;
	    }

# undef BUF_LEN
	} while (0);

	tcdrain(fd);
	close(fd);
	fd = -1;

    } while(0);
out:
    close(nd);
    return fd;
}

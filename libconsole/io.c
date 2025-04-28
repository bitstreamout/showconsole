/*
 * io.c
 *
 * Copyright 2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <limits.h>
#include <poll.h>
#include <time.h>
#include "libconsole.h"

/*
 * Check if we can read/write on a file descriptor
 * The timeout has to milli seconds, a negative value
 * might block if file descriptor is in blocking mode.
 * The sigset_t omask is given in console.c
 *
 */
int can_read(int fd, const time_t msec)
{
    struct pollfd fds = {
	.fd = fd,
	.events = POLLIN|POLLPRI,
	.revents = 0,
    };
    struct timespec ts = {
	.tv_sec = (time_t)(msec/1000),
	.tv_nsec = (msec % 1000) * 1000000,
    };
    int ret;

    do {
	ret = ppoll(&fds, 1, (msec < 0) ? NULL : &ts, &omask);
    } while ((ret < 0) && (errno == EINTR));

    return (ret == 1) && (fds.revents & (POLLIN|POLLPRI));
}

int can_write(int fd, const time_t msec)
{
    struct pollfd fds = {
	.fd = fd,
	.events = POLLOUT|POLLWRBAND,
	.revents = 0,
    };
    struct timespec ts = {
	.tv_sec = (time_t)(msec/1000),
	.tv_nsec = (msec % 1000) * 1000000,
    };
    int ret;

    do {
	ret = ppoll(&fds, 1, (msec < 0) ? NULL : &ts, &omask);
    } while ((ret < 0) && (errno == EINTR));

    return (ret == 1) && (fds.revents & (POLLOUT|POLLWRBAND));
}

void clear_input(int fd)
{
    unsigned char buf[LINE_MAX];
    ssize_t len;

    do {
	    if (!can_read(fd, 0))
		break;

	    len = read (fd, buf, LINE_MAX);
	    if (len < 0) {
		if (errno == EINTR)
		    continue;
		if (errno != EAGAIN)
		    warn("strange error on %d file descriptor", fd);
		break;
	    }

    } while (len);
}

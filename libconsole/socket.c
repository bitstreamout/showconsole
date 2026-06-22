/*
 * socket.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include "listing.h"
#include "libconsole.h"

int open_un_socket_and_listen(void)
{
    struct sockaddr_un su = {	/* The abstract UNIX socket of plymouth */
	.sun_family = AF_UNIX,
	.sun_path = PLYMOUTH_SOCKET_PATH,
    };
    const int one = 1;
    int fd, ret;

    fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
    if (fd < 0) {
	warn("can not open UNIX socket");
	goto err;
    }

    ret = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, (socklen_t)sizeof(one));
    if (ret < 0) {
	close(fd);
	fd = -1;
	warn("can not set option for UNIX socket");
	goto err;
    }

    ret = bind(fd, &su, offsetof(struct sockaddr_un, sun_path) + 1 + strlen(su.sun_path+1));
    if (ret < 0) {
	int err = errno;
	close(fd);
	fd = -1;
	if (err != EADDRINUSE)
	    warn("can not bind a name to UNIX socket");
	else
	    fd = -2;
	goto err;
    }

    ret = listen(fd, SOMAXCONN);
    if (ret < 0) {
	close(fd);
	fd = -1;
	warn("can not listen on UNIX socket");
	goto err;
    }
err:
    return fd;
}

int open_un_socket_and_connect(void)
{
    struct sockaddr_un su = {	/* The abstract UNIX socket of plymouth */
	.sun_family = AF_UNIX,
	.sun_path = PLYMOUTH_SOCKET_PATH,
    };
    const int one = 1;
    int fd, ret;

    fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
    if (fd < 0) {
	warn("can not open UNIX socket");
	goto err;
    }

    ret = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, (socklen_t)sizeof(one));
    if (ret < 0) {
	warn("can not set option for UNIX socket");
	close(fd);
	fd = -1;
	goto err;
    }

    ret = connect(fd, &su, offsetof(struct sockaddr_un, sun_path) + 1 + strlen(su.sun_path+1));
    if (ret < 0) {
	if (errno != ECONNREFUSED)
	    warn("can not connect on UNIX socket");
	close(fd);
	fd = -1;
	goto err;
    }
err:
    return fd;
}


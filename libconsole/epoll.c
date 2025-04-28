/*
 * epoll.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include "listing.h"
#include "libconsole.h"

/*
 * Handle extended polls
 */
extern int epfd;
extern int evmax;

static list_t lpolls = { &(lpolls), &(lpolls) };
static struct epolls {
    list_t watch;
    void (*handle)(int);
    int fd;
} *epolls;

static inline void epoll_addition(int fd, void *fptr, uint32_t flags)
{
    struct epoll_event ev = {};
    struct epolls *ep;
    list_t *head;
    int ret;

    ev.events = flags;

    if (posix_memalign((void**)&ep, sizeof(void*), alignof(struct epolls)) != 0 || !ep)
	error("memory allocation");

    ep->handle = (typeof(ep->handle))fptr;
    ep->fd = fd;

    ev.data.ptr = (void*)ep;

    if (!epolls) {
	head = &lpolls;
	epolls = (struct epolls*)head;
    } else
	head = &epolls->watch;
    insert(&ep->watch, head);
    evmax++;

    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0)
	error("can not add %d file descriptor on epoll file descriptor", fd);
}

void epoll_addread(int fd, void *fptr)
{
    epoll_addition(fd, fptr, EPOLLIN|EPOLLPRI|EPOLLRDHUP);
}

void epoll_addwrite(int fd, void *fptr)
{
    epoll_addition(fd, fptr, EPOLLOUT|EPOLLONESHOT|EPOLLPRI|EPOLLERR);
}

void epoll_answer_once(int fd, void *fptr)
{
    struct epoll_event ev = {};
    struct epolls *ep;
    list_t *head;
    int ret;

    ev.events = EPOLLOUT|EPOLLONESHOT;

    if (posix_memalign((void**)&ep, sizeof(void*), alignof(struct epolls)) != 0 || !ep)
	error("memory allocation");

    ep->handle = (typeof(ep->handle))fptr;
    ep->fd = fd;

    ev.data.ptr = (void*)ep;

    if (!epolls) {
	head = &lpolls;
	epolls = (struct epolls*)head;
    } else
	head = &epolls->watch;
    insert(&ep->watch, head);
    evmax++;

    ret = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    if (ret < 0)
	error("can not add %d file descriptor on epoll file descriptor", fd);
}

void epoll_reenable(int fd)
{
    struct epoll_event ev = {};
    struct epolls *ep;

    ev.events = EPOLLOUT|EPOLLONESHOT;

    list_for_each_entry(ep, &lpolls, watch) {
	if (ep->fd == fd) {
	    int ret;
	    ev.data.ptr = (void*)ep;
	    ret = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	    if (ret < 0)
		error("can not add %d file descriptor on epoll file descriptor", fd);
	    break;
	}
    }

}

void epoll_delete(int fd)
{
    struct epolls *ep, *n;
    int ret;

    list_for_each_entry_safe(ep, n, &lpolls, watch) {
	if (ep->fd == fd) {
	    delete(&ep->watch);
	    free(ep);
	    break;
	}
    }
    evmax--;

    ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    if (ret < 0)
	error("can not delete %d file descriptor on epoll file descriptor", fd);
}

void (*epoll_handle(void *ptr, int *fd))(int)
{
    struct epolls *ep;
    void (*handle)(int) = NULL;

    list_for_each_entry(ep, &lpolls, watch) {
	if (ep == ptr) {
	    handle = ep->handle;
	    *fd = ep->fd;
	    break;
	}
    }

    return handle;
}

/* Only usefull within forked sub processes */
void epoll_close_fd(void)
{
    struct epolls *ep;

    list_for_each_entry(ep, &lpolls, watch)
	close(ep->fd);
}

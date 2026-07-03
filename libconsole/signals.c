/*
 * signals.c
 *
 * Copyright 2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <stddef.h>
#include <signal.h>
#ifndef NO_SIGNALFD
# include <sys/signalfd.h>
# include <string.h>
#endif
#include "libconsole.h"

weak_symbol(pthread_sigmask);

/*
 * Set and reset signal handlers as well unblock the signal
 * if a handler for that signal is set.
 */
void set_signal(int sig, struct sigaction *old, sighandler_t handler)
{
    if (old) {
	do {
	    if (sigaction(sig, NULL, old) == 0)
		break;
	} while (errno == EINTR);
    }

    if (!old || (old->sa_handler != handler)) {
	struct sigaction new;
	sigset_t sigset;

	new.sa_handler = handler;
	sigemptyset(&new.sa_mask);
	new.sa_flags = SA_RESTART;
	do {
	    if (sigaction(sig, &new, NULL) == 0)
		break;
	} while (errno == EINTR);

	sigemptyset(&sigset);
	sigaddset(&sigset, sig);

	if ((pthread_sigmask))
		pthread_sigmask(SIG_UNBLOCK, &sigset, NULL);
	else
		sigprocmask(SIG_UNBLOCK, &sigset, NULL);
    }
}

void reset_signal(int sig, struct sigaction *old)
{
    struct sigaction cur;

    do {
	if (sigaction(sig, NULL, &cur) == 0)
	    break;
    } while (errno == EINTR);

    if (old && old->sa_handler != cur.sa_handler) {
	do {
	    if (sigaction(sig, old, NULL) == 0)
		break;
	} while (errno == EINTR);
    }
}

int restart_sig(int sig, int flag)
{
    struct sigaction cur;
    int ret = 0;

    do {
	if (sigaction(sig, NULL, &cur) == 0)
	    break;
    } while (errno == EINTR);

    if (flag)
	cur.sa_flags |= SA_RESTART;
    else
	cur.sa_flags &= ~SA_RESTART;

    do {
	if ((ret = sigaction(sig, &cur, NULL)) == 0)
	    break;
    } while (errno == EINTR);

    return ret;
}

#ifndef NO_SIGNALFD
/* Part of signal handling with signalfd only valid for libconsole */

static void handle_signal_event(int fd)
{
    struct signalfd_siginfo fdsi;

    ssize_t s = read(fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo))
	return;		/* Short reads are impossible by kernel design.
			   Catching -1 (e.g. EAGAIN) here. */

    switch (fdsi.ssi_signo) {
    case SIGTERM:
    case SIGQUIT:
    case SIGINT:
	if (nsigsys && (fdsi.ssi_signo == SIGTERM))
	    break;
	signaled = fdsi.ssi_signo;
	break;
    case SIGSYS:
	nsigsys = SIGSYS;
	break;
    case SIGIO:
	nsigio = SIGIO;
	break;
    case SIGCHLD:
	sigchild++;
	break;
    default:
	warn("Signal catched %s but not handled", strsignal(fdsi.ssi_signo));
	break;
    }
}

int setup_signalfd(sigset_t mask)
{
    int sfd = -1;

    if ((pthread_sigmask))
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
    else
	sigprocmask(SIG_BLOCK, &mask, NULL);

    /*
     * The SFD_NONBLOCK is essential for (e)polling
     * whereas SFD_CLOEXEC we might not need.
     * Nevertheless how to handle the logging thread
     * as well as the forking password questioners
     */
    sfd = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
    if (sfd < 0)
	error("can not open signal file descriptor");

    epoll_addread(sfd, &handle_signal_event);

    return sfd;
}
#endif

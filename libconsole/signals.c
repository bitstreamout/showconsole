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

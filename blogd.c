/*
 * blogd.c
 *
 * Copyright 2000,2015 Werner Fink, 2000 SuSE GmbH Nuernberg, Germany.
 * Copyright 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef  _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <sys/time.h>
#include <sys/types.h> /* Defines the macros major and minor */
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <sys/kd.h>
#include <time.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <paths.h>
#include <linux/major.h>
#include "libconsole.h"
#ifndef  _POSIX_MAX_CANON
# define _POSIX_MAX_CANON 255
#endif

extern volatile sig_atomic_t nsigsys;
extern volatile sig_atomic_t signaled;
extern volatile sig_atomic_t asking;
extern int final;

static int show_status;
static const char console[] = "/dev/console";
static const char *varrun = _PATH_VARRUN;

static void _initialize(void) __attribute__((__constructor__));
static void _initialize(void)
{
    const char *run;
    run = realpath(varrun, NULL);
    if (run && *run)
	varrun = run;
}

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
 * Remove pidfile
 */
static const char *myname;
static char *pidfile;
static char *plymouth;
static void rmfpid()
{
    if (!pidfile || *pidfile == 0)
	return;
    unlink(pidfile);
    if (plymouth && *plymouth)
	unlink(plymouth);
}

/*
 * Write pidfile
 */
static void attribute((noinline)) dopidfile()
{
    FILE * fpid;
    int ret;

    ret = asprintf(&pidfile, "%s/%s.pid", varrun, myname);
    if (ret < 0)
	error("can not allocate memory for pid file");

    if ((fpid = fopen (pidfile, "w")) == NULL) {
	warn("can not open %s", pidfile);
	goto out;
    }
    fprintf(fpid, "%d\n", getpid());
    fclose(fpid);

    ret = mkdir("/run/plymouth", 0755);
    if (ret < 0) {
	if (errno != EEXIST)
	    warn("can not make directory /run/plymouth");
	else
	    ret = 0;
    }
    if (ret == 0) {
	plymouth = "/run/plymouth/pid";
	ret = symlink(pidfile, plymouth);
	do {
	    int serr = errno;
	    int fd;

	    if (ret >= 0)
		break;

	    fd = open(plymouth, O_RDONLY|O_NOFOLLOW);
	    if (fd < 0 && errno == ELOOP) {
		    unlink(plymouth);
		    ret = symlink(pidfile, plymouth);
		    break;
	    }
	    if (fd >= 0) {
		char bpid[128];
		ssize_t len;

		len = read(fd, &bpid[0], sizeof(bpid)-1);
		close(fd);
		if (len > 0) {
		    pid_t pid;
		    char *exe;

		    pid = (pid_t) strtol(&bpid[0], &exe, 10);
		    if (pid > 1 && (!exe || !*exe || *exe == '\n')) {
			exe = proc2exe(pid);
			if (exe) {
			    errno = EACCES;
			    error("plymouth is active %ld", (long int)pid);
			}
		    }
		    unlink(plymouth);
		    ret = symlink(pidfile, plymouth);
		    break;
		}
	    }
	    errno = serr;
	    warn("can not create %s", plymouth);
	    plymouth = NULL;

	} while(0);
    }
    atexit(rmfpid);
out:
    return;
}

/*
 *  Signal handler
 */
static struct sigaction saved_sigttin;
static struct sigaction saved_sigttou;
static struct sigaction saved_sigtstp;
static struct sigaction saved_sighup;
static struct sigaction saved_sigint;
static struct sigaction saved_sigquit;
static struct sigaction saved_sigterm;
static struct sigaction saved_sigsys;
static struct sigaction saved_sigpipe;

static void sighandle(int sig)
{
    if (nsigsys && (sig == SIGTERM))
	return;
    signaled = (volatile sig_atomic_t)sig;
}

/*
 * Stop writing logs to disk, only repeat messages
 */
static void sigsys(int sig)
{
    nsigsys = (volatile sig_atomic_t)sig;
}

/*
 * To be able to reconnect to real tty on EIO
 */
static void reconnect(int fd)
{
    struct console * c;

    list_for_each_entry(c, &cons->node, node) {
	int newfd;

	if (!c->tty) continue;

	if (c->fd != fd) continue;

	switch (c->fd) {
	case 0:
	case -1:	/* Weired */
	    break;
	default:	/* IO of system consoles */
	    if ((newfd = open(c->tty, O_WRONLY|O_NONBLOCK|O_NOCTTY)) < 0)
		error("can not open %s", c->tty);
	    dup2(newfd, c->fd);
	    if (newfd != c->fd)
		close(newfd);
	    break;
	}
    }
}


static volatile pid_t pid = -1;

static void flush_handler (void) attribute((noinline));
static void exit_handler (void) attribute((noinline));

/*
 * Now do the job
 */
int main(int argc, char *argv[])
{
    char ptsname[NAME_MAX+1];
    const char *tty, *stt;
    volatile char *arg0;
    struct console *c;
    struct termios o;
    struct winsize w;
    struct stat st;
    time_t tt;
    int ptm, pts, fd, arg;
    int listen, pipefd[2];

    listen = open_un_socket_and_listen();
    if (listen < -1)
	_exit(EXIT_SUCCESS);

    if (stat("/run/systemd/show-status", &st) == 0)
	show_status = 1;
    if (kill (1, SIGRTMIN+20) < 0)
	warn("could not tell system to show its status");

    arg0 = (volatile char*)argv[0];
    remember_arg0(arg0);

    while ((arg = getopt(argc, argv, "f")) != -1) {
	switch (arg) {
	case 'f':
	    final = 1;
	    break;
	case '?':
	default:
	    return 1;
	}
    }
    argv += optind;
    argc -= optind;

    myname = program_invocation_short_name;
    getconsoles(&cons, 1);

    close(0);
    close(1);
    close(2);
    if ((fd = open(console, O_RDWR|O_NONBLOCK|O_NOCTTY)) < 0)
	error("Can not open system console %s", console);

    if (fd > 0) {
	(void)ioctl(fd, TIOCNXCL);	/* Avoid EBUSY */
	dup2(fd, 0);
	close(fd);
    }

    dup2(0, 1);
    dup2(0, 2);
    tty = console;

    (void)ioctl(0, TIOCCONS, NULL);  /* Undo any current map if any */

    list_for_each_entry(c, &cons->node, node) {
	speed_t ospeed;
	speed_t ispeed;
	int flags;

	ispeed = B38400;
	ospeed = B38400;

	if (ioctl(c->fd, TIOCMGET, &flags) == 0) {  /* serial line */
	    ispeed = cfgetispeed(&c->otio);
	    ospeed = cfgetospeed(&c->otio);
	    c->flags |= CON_SERIAL;
	}
	cfsetispeed(&c->otio, ispeed);
	cfsetospeed(&c->otio, ospeed);

	if (c->flags & CON_CONSDEV) {

	    tty = c->tty;

	    w.ws_row = 0;
	    w.ws_col = 0;
	    if (ioctl(c->fd, TIOCGWINSZ, &w) < 0)
		error("can not get window size of %s", c->tty);

	    if (!w.ws_row)
		w.ws_row = 24;
	    if (!w.ws_col)
		w.ws_col = 80;

	    memcpy(&o, &c->otio, sizeof(o));
	    cfmakeraw(&o);
	    cfsetispeed(&o, ispeed);
	    cfsetospeed(&o, ospeed);
	    o.c_lflag &= ~ECHO;
	    o.c_lflag |= ISIG;
	    o.c_cc[VTIME] = 0;
	    o.c_cc[VMIN]  = CMIN;
	}
    }

    if (openpty(&ptm, &pts, ptsname, &o, &w) < 0)
	error("can not open pty/tty pair");

    if (fstat(pts, &st) < 0)
	error("can not stat slave pty");
    else {
	struct termios lock;
	memset(&lock, 0xff, sizeof(lock));
	(void)ioctl(pts, TIOCSLCKTRMIOS, &lock);
    }

    if (ioctl(pts, TIOCCONS, NULL) < 0)
	error("can not set console device to %s", ptsname);

    /*
     * Reconnecting to stdin aka ptm to 0 is done after we fork away
     */
#ifdef DEBUG
    int l;
    if ((l = open("/dev/shm/blog.out", O_WRONLY|O_NOCTTY|O_NONBLOCK|O_CREAT|O_APPEND, S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH)) >= 0) {
	dup2(l, STDOUT_FILENO);
	dup2(l, STDERR_FILENO);
	close(l);
    } else
#endif
    {
	dup2(pts,  1);
	dup2(pts,  2);	/* Now we are blind upto safeIO() loop */
    }
    if (pts > 2)
	close(pts);

    asking = signaled = nsigsys = 0;
    set_signal(SIGTTIN, &saved_sigttin, SIG_IGN);
    set_signal(SIGTTOU, &saved_sigttou, SIG_IGN);
    set_signal(SIGTSTP, &saved_sigtstp, SIG_IGN);
    set_signal(SIGHUP,  &saved_sighup,  SIG_IGN);
    set_signal(SIGPIPE, &saved_sigpipe, SIG_IGN);
    set_signal(SIGINT,  &saved_sigint,  sighandle);
    set_signal(SIGQUIT, &saved_sigquit, sighandle);
    set_signal(SIGTERM, &saved_sigterm, sighandle);
    set_signal(SIGSYS,  &saved_sigsys,  sigsys);
    (void)restart_sig(SIGINT,  1);
    (void)restart_sig(SIGQUIT, 1);
    (void)restart_sig(SIGTERM, 1);
    (void)restart_sig(SIGSYS,  1);

    list_for_each_entry(c, &cons->node, node) {
	struct termios oldtio;
	speed_t ospeed;
	speed_t ispeed;
	int flags;

	(void)ioctl(c->fd, TIOCNXCL);	/* Avoid EBUSY */

#ifdef _PC_MAX_CANON
	if ((c->max_canon = (ssize_t)fpathconf(c->fd, _PC_MAX_CANON)) <= 0)
#endif
	    c->max_canon = _POSIX_MAX_CANON;
	c->tlock = 0;
	if (tcgetattr(c->fd, &c->otio) < 0)
	    continue;
	c->tlock = 1;

	if (ioctl(c->fd, TIOCGLCKTRMIOS, &c->ltio) == 0) {
	    /*
	     * Remove any lock as e.g. startpar(8) will use it
	     */
	    struct termios lock;
	    memset(&lock, 0x00, sizeof(lock));
	    if (ioctl(c->fd, TIOCSLCKTRMIOS, &lock) == 0)
		c->tlock = 2;
	}

	oldtio = c->otio;				/* Remember old tty I/O flags */

	if (ioctl(c->fd, TIOCMGET, &flags) == 0) {	/* serial line */
		ispeed = cfgetispeed(&c->otio);
		ospeed = cfgetospeed(&c->otio);

		c->otio.c_iflag  = c->otio.c_lflag = 0;
		c->otio.c_oflag  = (ONLCR | OPOST);
		c->otio.c_oflag &= ~(OLCUC);
		c->otio.c_cflag  = CREAD | CS8 | HUPCL | (c->otio.c_cflag & CLOCAL);

		cfsetispeed(&c->otio, ispeed);
		cfsetospeed(&c->otio, ospeed);
	} else {
		ioctl(fd, KDSETMODE, KD_TEXT);		/* Enforce text mode */

		c->otio.c_iflag |= (ICRNL | IGNBRK);
		c->otio.c_iflag &= ~(INLCR | IGNCR | BRKINT);
		c->otio.c_oflag |= (ONLCR | OPOST);
		c->otio.c_oflag &= ~(OCRNL | ONLRET | OLCUC);
	}

	(void)tcsetattr(c->fd, TCSADRAIN, &c->otio);

	c->ctio = c->otio;				/* Current setting */
	c->otio = oldtio;				/* To be able to restore */
    }

    atexit(exit_handler);	/* Register main exit handler */

    /* Make the first byte in argv be '@' so that we can survive systemd's killing
     * spree when going from initrd to /, and so we stay alive all the way until
     * the power is killed at shutdown.
     * http://www.freedesktop.org/wiki/Software/systemd/RootStorageDaemons
     */
    if (access("/etc/initrd-release", F_OK) >= 0 || final)
	arg0[0] = '@';

    if (pipe2(pipefd, O_CLOEXEC) < 0)
	pipefd[0] = pipefd[1] = -1;

    switch ((pid = fork())) {
    case 0:
	/* Write pid file */
	dopidfile();
	/* Get our own session */
	setsid();
	/* Reconnect our own terminal I/O */
	dup2(ptm, 0);
	{
	    const char *msg = "can not get terminal flags of stdin\n";
	    int flags;
	    int ret __attribute__ ((unused));

	    if ((flags = fcntl(0, F_GETFL)) < 0)
		list_for_each_entry(c, &cons->node, node) {
		    if (c->fd < 0)
			continue;
		    ret = write(c->fd, msg, strlen(msg));
		}
	    else {
		flags &= ~(O_NONBLOCK);
	        flags |=   O_NOCTTY;
		if (fcntl(0, F_SETFL, flags) < 0)
		    list_for_each_entry(c, &cons->node, node) {
			if (c->fd < 0)
			    continue;
			ret = write(c->fd, msg, strlen(msg));
		    }
	    }
	}
	if (ptm > 0)
	    close(ptm);
	if (pipefd[0] >= 0)
	    close(pipefd[0]);
	break;
    case -1:
	if (ptm > 0)
	    close(ptm);
	if (pipefd[0] >= 0)
	    close(pipefd[0]);
	if (pipefd[1] >= 0)
	    close(pipefd[1]);
	list_for_each_entry(c, &cons->node, node) {
	    const char *msg = "blogd: can not fork to become daemon: ";
	    const char *err = strerror(errno);
	    int ret __attribute__ ((unused));
	    if (c->fd < 0)
		continue;
	    ret = write(c->fd, msg, strlen(msg));
	    ret = write(c->fd, err, strlen(err));
	    ret = write(c->fd, "\n", 1);
	}
	exit(EXIT_FAILURE);
    default:
	time(&tt);
	stt = ctime(&tt);
	close(ptm);
	if (pipefd[1] >= 0)
	    close(pipefd[1]);
	list_for_each_entry(c, &cons->node, node) {
	    if (c->fd > 0) {
		close(c->fd);
		c->fd = -1;
	    }
	}
	fflush(stdout);
	fprintf(stdout, "\rBoot logging started on %s(%s) at %.24s\n", tty, console, stt);
	fflush(stdout);

	if (pipefd[0] > 0) {
	    int dummy, ret __attribute__ ((unused));
	    ret = read(pipefd[0], &dummy, 1);
	    close(pipefd[0]);
	}

	_exit(EXIT_SUCCESS);	/* NEVER rise exit handlers here */
    }

    atexit(flush_handler);	/* Register flush exit handler */

    prepareIO(reconnect, listen, 0);

    if (pipefd[1] >= 0)
	close(pipefd[1]);

    while (!signaled)
	safeIO();

    exit(EXIT_SUCCESS);		/* Raise exit handlers */
}

static void flush_handler (void)
{
    (void)tcdrain(1);
    (void)tcdrain(2);
    closeIO();
}

static void exit_handler (void)
{
    struct console *c;
    int fd;

    if (show_status == 0 && kill (1, SIGRTMIN+20) < 0)
	warn("could not tell system to hode its status");

    list_for_each_entry(c, &cons->node, node) {
	if (c->fd > 0) {
	    if (c->tlock)	/* write back old setup  */
		tcsetattr(c->fd, TCSADRAIN, &c->otio);
	    if (c->tlock > 1)	/* write back lock if any */
		(void)ioctl(c->fd, TIOCSLCKTRMIOS, &c->ltio);
	    c->tlock = 0;
	    if (c->fd > 1)
		close(c->fd);
	    c->fd = -1;
	}
    }

    errno = 0;
    if ((fd = open(console, O_RDWR|O_NOCTTY)) >= 0) {
	(void)ioctl(fd, TIOCCONS, NULL);	/* Restore old console mapping */
	if (fd > 0)
	    close(fd);
    }

    close(1);
    close(2);
    (void)tcflush(0, TCIFLUSH);
    close(0);

    reset_signal(SIGTTIN, &saved_sigttin);
    reset_signal(SIGTTOU, &saved_sigttou);
    reset_signal(SIGTSTP, &saved_sigtstp);
    reset_signal(SIGHUP,  &saved_sighup);
    reset_signal(SIGPIPE, &saved_sigpipe);
    reset_signal(SIGINT,  &saved_sigint);
    reset_signal(SIGQUIT, &saved_sigquit);
    reset_signal(SIGTERM, &saved_sigterm);
    reset_signal(SIGSYS,  &saved_sigsys);
}

/*
 * console.c
 *
 * Copyright 2000,2015 Werner Fink, 2000 SuSE GmbH Nuernberg, Germany.
 * Copyright 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/magic.h>
#include <linux/major.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include "listing.h"
#include "libconsole.h"

#undef BLOGD_EXT
#ifndef  _GNU_SOURCE
# define _GNU_SOURCE
#endif
#ifndef  BOOT_LOGFILE
# define BOOT_LOGFILE		"/var/log/boot.log"
#endif
#ifndef  BOOT_OLDLOGFILE
# define BOOT_OLDLOGFILE	"/var/log/boot.old"
#endif
#ifndef  _PATH_BLOG_FIFO
# define _PATH_BLOG_FIFO	"/dev/blog"
#endif

int final = 0;

/*
 * Used to ignore some signals during epoll_pwait(2) or ppoll(2)
 */
sigset_t omask;

/*
 * Handle extended polls
 */
int epfd  = -1;
int evmax;
static int requests = 0;

#define REQUEST_CLOSE	(1<<0)

/*
 * Remember if we're signaled.
 */
volatile sig_atomic_t signaled;

/*
 * Error raised in exit handler should not call exit(3) its self
 */
#define lerror(fmt, args...)	    \
    do {			    \
	if (signaled) {		    \
	    warn(fmt, args);	    \
		goto out;	    \
	} error(fmt, args);	    \
    } while (0)

static volatile sig_atomic_t sigchild;
static void chld_handler(int sig) {
    ++sigchild;
}

/*
 * Arg used: safe out
 */
static void (*vc_reconnect)(int fd);
void safeout (int fd, const void *ptr, size_t s, ssize_t max)
{
    int saveerr = errno;

    while (s > 0) {
	ssize_t p = write (fd, ptr, (max < 1) ? 1 : ((s < (size_t)max) ? s : (size_t)max));
	if (p < 0) {
	    if (errno == EPIPE)
		break;

	    if (errno == EINTR) {
		errno = 0;
		continue;
	    }
	    if (errno == EAGAIN || errno == EWOULDBLOCK) {

		/* Avoid high load: wait upto two seconds if system is not ready */
		if (can_write(fd, 2))
		    continue;
	    }
	    if (errno == EIO) {
		if (!vc_reconnect)
		    lerror("can not write to fd %d", fd);

		(*vc_reconnect)(fd);
		errno = 0;
		continue;
	    }
	    lerror("can not write to fd %d", fd);
	}
	ptr += p;
	s -= p;
    }
out:
    errno = saveerr;
}

/*
 * Twice used: safe in
 */

static int safein_noexit = 0;
ssize_t safein (int fd, void *ptr, size_t s)
{
    int saveerr = errno;
    ssize_t r = 0;
    int t;
    static int repeated;

    if (s > SSIZE_MAX)
	s = SSIZE_MAX;

    t=0;
    if ((ioctl(fd, FIONREAD, &t) < 0) || (t == 0)) {

	do {
	    /* Avoid deadlock: do not read if nothing is in there */
	    if (!can_read(fd, 0))
		break;

	    r = read (fd, ptr, s);

	} while (r < 0 && (errno == EINTR || errno == EAGAIN));

	/* Do not exit on a broken FIFO */
	if (r < 0 && errno != EPIPE) {
	    if (safein_noexit || signaled)
		goto out;
	    if (fd == 0 && errno == EIO)
		warn("\e[31m\e[1msystem console stolen at line %d!\e[m", __LINE__);
	    lerror("Can not read from fd %d", fd);
	}

	goto out;
    }

    if (t > 0 && (size_t)t > s)
	t = s;

    repeated = 0;
    while (t > 0) {
	ssize_t p = read (fd, ptr, t);
	if (p < 0) {
	    if (repeated++ > 1000) {
		lerror("Repeated error on reading from fd %d", fd);
	    }
	    if (errno == EINTR || errno == EAGAIN) {
		errno = 0;
		continue;
	    }
	    if (safein_noexit || signaled)
		goto out;
	    if (fd == 0 && errno == EIO)
		warn("\e[31m\e[1msystem console stolen at line %d!\e[m", __LINE__);
	    lerror("Can not read from fd %d", fd);
	}
	repeated = 0;
	ptr += p;
	r += p;
	t -= p;
    }
out:
    errno = saveerr;
    return r;
}

/*
 * The stdio file pointer for our log file
 */
struct console *cons;
static FILE * flog = NULL;
static int fdread  = -1;
static int fdfifo  = -1;

static int fdsock  = -1;
static char *pwprompt;
static char *password;
static int32_t *pwsize;

/*
 * Signal control for writing on log file
 */
volatile sig_atomic_t nsigsys;
static volatile sig_atomic_t nsigio = -1;

/* One shot signal handler */
static void sigio(int sig)
{
    /* Currently no thread active */
    if (nsigio == 0)
	set_signal(sig, NULL, SIG_IGN);
    nsigio = sig;
}

/*
 * Our transfer buffer
 */
static char trans[TRANS_BUFFER_SIZE];

/*
 * Prepare I/O
 */
static const char *fifo_name = _PATH_BLOG_FIFO;
static void epoll_console_in(int) attribute((noinline));
static void epoll_fifo_in(int) attribute((noinline));
static void epoll_socket_accept(int) attribute((noinline));

void prepareIO(void (*rfunc)(int), const int listen, const int input)
{
    (void)sigfillset(&omask);
    (void)sigdelset(&omask, SIGQUIT);
    (void)sigdelset(&omask, SIGTERM);
    (void)sigdelset(&omask, SIGSYS);
    (void)sigdelset(&omask, SIGIO);

    vc_reconnect = rfunc;
    fdsock  = listen;
    fdread  = input;

    if (fifo_name && fdfifo < 0) {
	struct stat st;
	errno = 0;
	/* udev support: create /dev/blog if not exist */
	if (stat(fifo_name, &st) < 0 && errno == ENOENT)
	    (void)mkfifo(fifo_name, 0600);
	errno = 0;
	if (!stat(fifo_name, &st) && S_ISFIFO(st.st_mode)) {
	    if ((fdfifo = open(fifo_name, O_RDWR|O_NOCTTY|O_CLOEXEC)) < 0)
		warn("can not open named fifo %s", fifo_name);
	}
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
	error("can not open epoll file descriptor");

    if (fdread >= 0)
	epoll_addread(fdread, &epoll_console_in);
    if (fdfifo >= 0)
	epoll_addread(fdfifo, &epoll_fifo_in);
    if (fdsock >= 0)
	epoll_addread(fdsock, &epoll_socket_accept);

    (void)mlockall(MCL_FUTURE);
}

/*
 * Seek for input, more input ...
 */
static int more_input (int timeout, const int noerr)
{
    struct epoll_event evlist[evmax];
    int nfds, n, ret = 0;
    int saveerr = errno;

    memset(&evlist[0], 0, evmax * sizeof(struct epoll_event));

    errno = 0;
    nfds = epoll_pwait(epfd, &evlist[0], evmax, timeout, &omask);
    if (nfds < 0) {
	ret = (errno == EINTR);
	if (!ret)
	    error ("epoll_pwait()");
	goto out;
    }

    safein_noexit = noerr;  /* Do or do not not exit on unexpected errors */

    for (n = 0; n < nfds; n++) {
	if (evlist[n].events & (EPOLLIN|EPOLLPRI)) {
	    void (*efunc)(int);
	    int fd;
	    efunc = epoll_handle(evlist[n].data.ptr, &fd);
	    if (!efunc)
		continue;
	    efunc(fd);
	    ret = 1;
	}
    }

    for (n = 0; n < nfds; n++) {
	if (evlist[n].events & (EPOLLOUT)) {
	    void (*efunc)(int);
	    int fd;
	    efunc = epoll_handle(evlist[n].data.ptr, &fd);
	    if (!efunc)
		continue;
	    efunc(fd);
	}
    }

    safein_noexit = 0;

out:
    errno = saveerr;
    return ret;
}

/*
 *  The main routine for blogd.
 */
void safeIO (void)
{
    static int log = -1;
    static int atboot = 0;
    const char *logfile = BOOT_LOGFILE;

    if (!nsigio) /* signal handler set but no signal recieved */
	goto skip;

    if (log < 0) {
#ifdef DEBUG_SIGIO
	if (nsigio < 0)
	   goto skip;
#else
	if (nsigio < 0) {
	    struct statfs fst;
	    struct stat st;
	    int ret;

	    /*
	     * In initrd the path /var/log is a link to ../run/log or
	     * does not exists at all.
	     */

	    ret = lstat("/var/log", &st);
	    if (ret < 0) {
		warn("can not get file status of /var/log");
		goto skip;
	    }
	    if ((st.st_mode & S_IFMT) == S_IFLNK) {
		atboot = 1;
		goto skip;
	    }

	    /*
	     * Use statfs to check for tmpfs like TMPFS_MAGIC,
	     * RAMFS_MAGIC, SQUASHFS_MAGIC, CRAMFS_MAGIC, CRAMFS_MAGIC_WEND
	     * used for initrd.
	     */

	    ret = statfs("/var/log", &fst);
	    if (ret < 0) {
		warn("can not get file system status of /var/log");
		goto skip;
	    }
	    switch (fst.f_type) {
	    case TMPFS_MAGIC:
	    case RAMFS_MAGIC:
	    case SQUASHFS_MAGIC:
	    case CRAMFS_MAGIC:
	    case CRAMFS_MAGIC_WEND:
		atboot = 1;
		goto skip;
		break;
	    default:
		break;
	    }
	}
#endif
	if (final) {
	    int ret;
	    ret = unlink(BOOT_OLDLOGFILE);
	    if (ret < 0) {
		if (errno == EACCES || errno == EROFS || errno == EPERM)
		    goto skip;
		if (errno != ENOENT)
		    warn("Can not rename %s", logfile);
	    }
	    ret = rename(logfile, BOOT_OLDLOGFILE);
	    if (ret < 0) {
		if (errno == EACCES || errno == EROFS || errno == EPERM)
		    goto skip;
		if (errno != ENOENT)
		    error("Can not rename %s", logfile);
	    }
	    logfile = BOOT_OLDLOGFILE;
	}
	if (access(logfile, W_OK) < 0) {
	    if (errno != ENOENT && errno != EROFS)
		error("Can not write to %s", logfile);
	    if (errno == EROFS)
		goto skip;
	    if (errno == ENOENT && !final)
		atboot = 1;
	}
	if ((log = open(logfile, O_WRONLY|O_NOCTTY|O_NONBLOCK|O_CREAT|O_APPEND, S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH)) < 0) {
	    if (errno != ENOENT && errno != EROFS)
		error("Can not open %s", logfile);
	    goto skip;
	}
	flog = open_logging(log);

	nsigio = SIGIO; /* We do not need a signal handler */
	set_signal(SIGIO, NULL, SIG_IGN);
    }

skip:
    if (nsigio < 0) { /* signal handler not set, so do it */

	/* Currently no thread active */
	nsigio = 0;
	atboot = 1;

	set_signal(SIGIO, NULL, sigio);
    }

    if (flog) {
	if (atboot) {
	    dump_kmsg(flog);
	    atboot = 0;
	}
	start_logging();
    }

    (void)more_input(5000, 0);

    if (nsigsys) {  /* Stop writing logs to disk, only repeat messages */
	if (flog) {
	    stop_logging();
	    flog = close_logging();
	}
	if (requests & REQUEST_CLOSE) {
	    ;
	}
	if (nsigio < 0) {
	    nsigio = SIGIO;
	    (void)set_signal(SIGIO, NULL, SIG_IGN);
	}
    }
}

/*
 *
 */
void closeIO(void)
{
    struct console *c;
    int ret, n = 20;

    /* Maybe we've catched a signal, therefore */
    if (!flog && !nsigsys)
	warn("no message logging because /var file system is not accessible");

    list_for_each_entry(c, &cons->node, node) {
	if (c->fd < 0)
	    continue;
	(void)tcdrain(c->fd);		/* Hold in sync with console */
    }

    flushlog();

    do {
	/*
	 * Repeat this as long as required,
	 * but not more than 3 seconds
	 */

	if (!n)
	    break;
	n--;

    	ret = more_input(150, 1);
	(void)tcdrain(fdread);

	flushlog();

    } while (ret);

    stop_logging();
    flog = close_logging();

    if (requests & REQUEST_CLOSE) {
	;
    }

    if (fdfifo >= 0) {
	epoll_delete(fdfifo);
	close(fdfifo);
	fdfifo = -1;
    }

    if (fdsock >= 0) {
	epoll_delete(fdsock);
	close(fdsock);
	fdsock = -1;
    }

    epoll_close_fd();
    if (epfd >= 0)
	close(epfd);

    if (password)
	memset(password, 0, MAX_PASSLEN);

    list_for_each_entry(c, &cons->node, node) {
	if (c->fd < 0)
	    continue;
	(void)tcdrain(c->fd);
    }
    return;
}

static int consinitIO(struct console *newc)
{
    int tflags;

    if ((newc->fd = open(newc->tty, O_WRONLY|O_NONBLOCK|O_NOCTTY)) < 0) {
	if (errno == EACCES)
	    error("can not open %s", newc->tty);
	warn("can not open %s", newc->tty);
	return 0;
    }

    newc->tlock = 0;
    newc->max_canon = _POSIX_MAX_CANON;
    memset(&newc->ltio, 0, sizeof(newc->ltio));
    memset(&newc->otio, 0, sizeof(newc->otio));
    memset(&newc->ctio, 0, sizeof(newc->ctio));
    if ((tflags = fcntl(newc->fd, F_GETFL)) < 0)
	warn("can not get terminal flags of %s", newc->tty);

    tflags &= ~(O_NONBLOCK);
    tflags |=   O_NOCTTY;
    if (fcntl(newc->fd, F_SETFL, tflags) < 0)
	warn("can not set terminal flags of %s", newc->tty);

    return 1;
}

/* Allocate a console */
static list_t lcons = { &(lcons), &(lcons) };
static int consalloc(struct console **cons, char *name, const int cflags, const dev_t dev, int io)
{
    struct console *newc;
    list_t *head;

    if (!cons)
	error("missing console pointer");

    if (posix_memalign((void**)&newc, sizeof(void*), alignof(struct console)+strlen(name)+1) != 0 || !newc)
	error("memory allocation");

    newc->tty = ((char*)newc)+alignof(struct console);
    strcpy(newc->tty, name);
    newc->flags = cflags;
    newc->dev = dev;
    newc->pid = -1;

    if (io && !consinitIO(newc)) {
	free(newc);
	return 0;
    }

    if (!*cons) {
	head = &lcons;
	*cons = (struct console*)head;
    } else
	head = &(*cons)->node;
    insert(&newc->node, head);

    return 1;
}

void getconsoles(struct console **cons, int io)
{
    static const struct {
	short flag;
	char name;
    } con_flags[] = {
	{ CON_ENABLED,		'E' },
	{ CON_CONSDEV,		'C' },
	{ CON_BOOT,		'B' },
	{ CON_PRINTBUFFER,	'p' },
	{ CON_BRL,		'b' },
	{ CON_ANYTIME,		'a' },
    };
    struct console *c = NULL;
    char fbuf[16], dev[64];
    char *tty = NULL;
    FILE *fc;

    if (!cons)
	error("error: console pointer empty");

    fc = fopen("/proc/consoles", "re");
    if (!fc) {
	if (errno != ENOENT)
	    error("can not open /proc/consoles");
	warn("can not open /proc/consoles");
	goto err;
    }

    while ((fscanf(fc, "%*s %*s (%[^)]) %[0-9:]", &fbuf[0], &dev[0]) == 2)) {
	char *tmp;
	int flags, n, maj, min;
	int ret;

	if (!strchr(fbuf, 'E'))
	    continue;

	flags = 0;
	for (n = 0; n < sizeof(con_flags)/sizeof(con_flags[0]); n++)
	    if (strchr(fbuf, con_flags[n].name))
		flags |= con_flags[n].flag;

	ret = asprintf(&tty, "/dev/char/%s", dev);
	if (ret < 0)
	    error("can not allocate string");

	tmp = tty;
	tty = realpath(tty, NULL);
	if (!tty) {
	    if (errno != ENOENT && errno != ENOTDIR)
		error("can not determine real path of %s", tmp);

	    tty = charname(dev);
	    if (!tty)
		error("can not determine real path of %s", tmp);
	}
	free(tmp);

	if (sscanf(dev, "%u:%u", &maj, &min) != 2)
	    error("can not determine device numbers for %s", tty);

	consalloc(&c, tty, flags, makedev(maj, min), io);
	free(tty);
    }

    fclose(fc);

    if (!c)
	goto err;

     *cons = c;
    return;
err:
    tty = strdup("/dev/console");
    if (!tty)
	error("can not allocate string");

    if (!consalloc(&c, tty, CON_CONSDEV, makedev(TTYAUX_MAJOR, 1), io))
	error("/dev/console is not a valid fallback\n");

    *cons = c;
}

/*
 * Do handle the console in data
 */
static void epoll_console_in(int fd)
{
    const ssize_t cnt = safein(fdread, trans, sizeof(trans));
    static struct winsize owz;
    struct winsize wz;

    if (cnt > 0) {
	struct console *c;
	static int fdc = -1;
    	int saveerr = errno;

	if (fdc < 0) {
	    list_for_each_entry(c, &cons->node, node) {
		if (c->flags & CON_CONSDEV) {
		    fdc = c->fd;
		    break;
		}
	    }
	}

	if (fdc > 0 && ioctl(fdc, TIOCGWINSZ, &wz) == 0) {
	    if (memcmp(&owz, &wz, sizeof(struct winsize))) {
		ioctl(fd, TIOCSWINSZ, &wz);
		(void)memcpy(&owz, &wz, sizeof(struct winsize));
	    }
	}
	errno = saveerr;

	parselog(trans, cnt);				/* Parse and make copy of the input */

	list_for_each_entry(c, &cons->node, node) {
	    if (c->fd < 0)
		continue;
	    safeout(c->fd, trans, cnt, c->max_canon);
	    (void)tcdrain(c->fd);			/* Write copy of input to real tty */
	}

	flushlog();
    }
}

/*
 * Do handle the fifo in data
 */
static void epoll_fifo_in(int fd)
{
    const ssize_t cnt = safein(fd, trans, sizeof(trans));

    if (cnt > 0) {
	copylog(trans, cnt);		/* Make copy of the input */
	flushlog();
    }
}

/*
 * Do the answer on the password request
 */
extern void *frobnicate(void *in, const size_t len);
static int do_answer_password(int fd)
{
    if (!pwsize || *pwsize <= 0) {
	const char *enqry = ANSWER_ENQ;

	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);
	return 0;

    } else {
	const char *multi = ANSWER_MLT;
	const uint32_t len = *pwsize;
	uint32_t nel = len + 1;

	password = frobnicate(password, len);

	safeout(fd, multi, strlen(multi), strlen(multi));

	/* Why does plymouth use LE instead of NET/BE order */
	nel = htole32(nel);
	safeout(fd, &nel, sizeof(uint32_t), sizeof(uint32_t));

	safeout(fd, password, len+1, SSIZE_MAX);

	password = frobnicate(password, len);
    }

    if (pwprompt) {
	free(pwprompt);
	pwprompt = NULL;
    }

    return 1;
}

/*
 * Socket and password handling
 */
static void socket_handler(int fd) attribute((noinline));
static void epoll_socket_accept(int fd)
{
    int fdconn;

    fdconn = accept4(fd, NULL, NULL, SOCK_CLOEXEC|SOCK_NONBLOCK);
    if (fdconn < 0)
	warn("can not connect on UNIX socket");
    else
	epoll_addread(fdconn, &socket_handler);
}

static void ask_for_password(void) attribute((noinline));
static void epoll_socket_answer(int fd)
{
    if (fd < 0) {
	errno = EBADFD;
	warn("%s no connection jet", __FUNCTION__);
	return;
    }

    ask_for_password();
    (void)do_answer_password(fd);
    epoll_delete(fd);
    close(fd);
}

static void socket_handler(int fd)
{
    struct ucred cred = {};
    unsigned char magic[2];
    const char *enqry;
    char *arg = NULL;
    socklen_t clen;
    int ret = -1;

    if (fd < 0) {
	errno = EBADFD;
	warn("%s no connection jet", __FUNCTION__);
	goto out;
    }

    ret = safein(fd, &magic[0], sizeof(magic));
    if (ret < 0) {
	warn("can not read request magic from UNIX socket");
	goto out;
    }

    if (magic[1] == '\002') {
	unsigned char alen;

	ret = safein(fd, &alen, sizeof(unsigned char));
	if (ret < 0) {
	     warn("can not get message len from UNIX socket");
	     goto out;
	}

	arg = calloc(alen, sizeof(char));
	if (!arg)
	    error("can not allocate memory for message from socket");

	ret = safein(fd, arg, alen);
	if (ret < 0) {
	    warn("can not get message len from UNIX socket");
	    goto out;
	}
    }

    clen = sizeof(struct ucred);
    ret = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &clen);
    if (ret < 0) {
	list_fd(getpid());
	warn("can not get credentials from UNIX socket part1");
	goto out;
    }
    if (clen != sizeof(struct ucred)) {
	list_fd(getpid());
	list_fd(cred.pid);
	warn("can not get credentials from UNIX socket part2");
	goto out;
    }
    if (cred.uid != 0) {
	const char *nack = ANSWER_NCK;
	char *exe;

	safeout(fd, nack, strlen(nack)+1, SSIZE_MAX);

	exe = proc2exe(cred.pid);

	errno = EACCES;
	if (exe) {
	    warn("Connection from %s of user %lu", exe, (unsigned long)cred.uid);
	    free(exe);
	} else
	    warn("Connection from pid %lu user %lu", (unsigned long)cred.pid, (unsigned long)cred.uid);

	goto out;
    }

    switch (magic[0]) {
    case MAGIC_ASK_PWD:

	if (!password) {
	    password = (char*)shm_malloc(MAX_PASSLEN, MAP_LOCKED|MAP_ANONYMOUS|MAP_SHARED);
	    if (!password)
		error("can not allocate string for password");
	    pwsize = (int32_t*)mmap(NULL, sizeof(int32_t), PROT_READ|PROT_WRITE, MAP_LOCKED|MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	    if (pwsize == MAP_FAILED)
		error("can not allocate integer for password length");
	}
	password[0] = '\0';
	pwprompt = strdup(arg);

	epoll_answer_once(fd, &epoll_socket_answer);

	goto job;

    case MAGIC_CACHED_PWD:

	ret = do_answer_password(fd);
	if (ret == 0)
	    goto job;

	break;

    case MAGIC_CHROOT:

	new_root(arg);

	enqry = ANSWER_ACK;
	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);

	break;

    case MAGIC_SYS_INIT:

	enqry = ANSWER_ACK;
	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);

	if (nsigio == 0)
	    set_signal(SIGIO, NULL, SIG_IGN);
	nsigio = SIGIO;

	break;

    case MAGIC_PRG_STOP:
    case MAGIC_PRG_CONT:
    case MAGIC_UPDATE:
    case MAGIC_HIDE_SPLASH:
    case MAGIC_SHOW_SPLASH:
    case MAGIC_CHMOD:
    case MAGIC_DETAILS:
    case MAGIC_PING:

	enqry = ANSWER_ACK;
	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);

	break;

    case MAGIC_QUIT:

	enqry = ANSWER_ACK;
	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);

	if (nsigsys && (signaled == SIGTERM))
	    break;
	signaled = SIGTERM;

	break;

    case MAGIC_CLOSE:

	requests |= REQUEST_CLOSE;

	enqry = ANSWER_ACK;
	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);

	if (nsigsys == 0)
	    set_signal(SIGSYS, NULL, SIG_IGN);
	nsigsys = SIGSYS;

	break;

    default:

	enqry = ANSWER_NCK;
	safeout(fd, enqry, strlen(enqry)+1, SSIZE_MAX);

	break;
    }
out:			/* We are done */
    if (fd > 0) {
	epoll_delete(fd);
	close(fd);
    	fd = -1;
    }
job:			/* Do not close connection for reply */
    if (arg)
	free(arg);
    return;
}

/*
 * Do handle the connection in data
 */
static void ask_for_password(void)
{
    struct timespec timeout = {0, 50000000};
    siginfo_t status = {};
    sigset_t set = {};
    struct console *c;
    int wait = 0;

    if (!pwprompt)
	return;

    set_signal(SIGCHLD, NULL, chld_handler);

    /* pwprompt */
    list_for_each_entry(c, &cons->node, node) {

	if (c->fd < 0 || !c->tty)
	    continue;
	(void)tcdrain(c->fd);

	c->pid = fork();
	if (c->pid < 0)
	    error("failed to fork process");

	if (c->pid == 0) {
	    struct termios newtio;
	    struct console *d;
	    char *message;
	    int eightbit;
	    int len, fdc;

	    if (fdfifo >= 0)
		close(fdfifo);
	    if (fdsock >= 0)
		close(fdsock);
	    if (flog)
		(void)fclose(flog);
	    if (epfd) {
		epoll_close_fd();
		close(epfd);
	    }

	    dup2(1, 2);
	    dup2(c->fd, 0);
	    dup2(c->fd, 1);

	    list_for_each_entry(d, &cons->node, node)
		if (d->fd >= 0) {
		    close(d->fd);
		    d->fd = -1;
		}

	    (void) setsid();

	    set_signal(SIGHUP,  NULL, SIG_DFL);

	    prctl(PR_SET_PDEATHSIG, SIGHUP);

	    set_signal(SIGCHLD, NULL, SIG_DFL);
	    set_signal(SIGINT,  NULL, SIG_DFL);
	    set_signal(SIGTERM, NULL, SIG_DFL);
	    set_signal(SIGSYS,  NULL, SIG_DFL);

	    set_signal(SIGQUIT, NULL, SIG_IGN);

	    fdc = request_tty(c->tty);
	    if (fdc < 0)
		_exit(1);

	    dup2(fdc, 0);
	    dup2(fdc, 1);
	    close(fdc);

	    clear_input(0);

	    if (c->flags & CON_SERIAL)
		len = asprintf(&message, "\n\r%s: ", pwprompt);
	    else
		len = asprintf(&message, "\e[1m\r%s:\e[m ", pwprompt);
	    if (len < 0) {
		warn("can not set password prompt");
		_exit(1);
	    }
	    safeout(1, message, len, c->max_canon);
	    free(message);

	    /* We read byte for byte */
	    newtio = c->ctio;
	    newtio.c_iflag &= ~(IUCLC|IXON|IXOFF|IXANY);
	    newtio.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|TOSTOP|ICANON|ISIG);
	    newtio.c_cc[VTIME] = 0;
	    newtio.c_cc[VMIN] = 1;

	    if (tcsetattr(0, TCSANOW, &newtio) < 0)
		warn("can not make invisible");

	    eightbit = ((c->flags & CON_SERIAL) == 0 || (newtio.c_cflag & (PARODD|PARENB)) == 0);
	    *pwsize = readpw(0, password, eightbit);

	    tcsetattr(0, TCSANOW, &c->ctio);
	    safeout(1, "\n", 1, c->max_canon);

	    if (*pwsize < 0)
		warn("can not read password");

	    password = frobnicate(password, *pwsize);
	    _exit(0);
	}
    }

    do {					/* Wait on any job if any */
	int ret;

	status.si_pid = 0;
	ret = waitid(P_ALL, 0, &status, WEXITED);

	if (ret == 0)
		break;

	if (ret < 0) {
	    if (errno == ECHILD)
		break;
	    if (errno == EINTR)
		continue;
	}

	error("can not wait on password asking process");

    } while (1);

    list_for_each_entry(c, &cons->node, node) {
	if (c->fd < 0)
	    continue;
	if (c->pid < 0)
	    continue;
	if (c->pid == status.si_pid)		/* Remove first reply ... */
	    c->pid = -1;
	else {
	    kill(c->pid, SIGTERM);		/* and terminate the others */
	    wait++;
	}
    }

    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);			/* On exit we'll see SIGCHLD */

    do {
	int signum, ret;

	if (!wait)
	    break;

	status.si_pid = 0;
	ret = waitid(P_ALL, 0, &status, WEXITED|WNOHANG);

	if (ret < 0) {
	    if (errno == ECHILD)
		break;
	    if (errno == EINTR)
		continue;
	}

	if (!ret && status.si_pid > 0) {
	    list_for_each_entry(c, &cons->node, node) {
		if (c->pid < 0)
		    continue;
		if (c->pid == status.si_pid) {
		    c->pid = -1;
		    wait--;
		}
	    }
	    continue;
	}

	signum = sigtimedwait(&set, NULL, &timeout);
	if (signum != SIGCHLD) {
	    if (signum < 0 && errno == EAGAIN)
		break;
	}

    } while (1);
}


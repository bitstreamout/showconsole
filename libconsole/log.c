/*
 * log.c
 *
 * Copyright 2000,2015 Werner Fink, 2000 SuSE GmbH Nuernberg, Germany.
 * Copyright 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "listing.h"
#include "libconsole.h"

#undef BLOGD_EXT
#ifndef  _GNU_SOURCE
# define _GNU_SOURCE
#endif
#ifndef  LOG_BUFFER_SIZE
# define LOG_BUFFER_SIZE	65536
#endif

/*
 * The stdio file pointer for our log file
 */
static FILE *flog = NULL;

/*
 * Signal control for writing on log file
 */
extern volatile sig_atomic_t nsigsys;

/*
 * Our ring buffer
 */
typedef struct _mutex {
    volatile int locked;
    volatile int canceled;
    volatile int used;
    pthread_mutex_t mutex;
    pthread_t thread;
} mutex_t;

static inline void lock(mutex_t *mutex)
{
    if (mutex->thread != pthread_self() || !mutex->locked) {
	pthread_mutex_lock(&mutex->mutex);
	mutex->thread = pthread_self();
    }
    mutex->locked++;
}

static inline void unlock(mutex_t *mutex)
{
    if (!--mutex->locked) {
	mutex->thread = 0;
	pthread_mutex_unlock(&mutex->mutex);
    }
}

static mutex_t llock = { 0, 0, 1, PTHREAD_MUTEX_INITIALIZER, 0 };
static mutex_t ljoin = { 0, 0, 1, PTHREAD_MUTEX_INITIALIZER, 0 };
static pthread_cond_t lcond = PTHREAD_COND_INITIALIZER;
static pthread_t    lthread;
static volatile int running;

static       unsigned char data[LOG_BUFFER_SIZE];
static const unsigned char *const end = data + sizeof(data);
static       unsigned char *     head = data;
static       unsigned char *     tail = data;
static volatile ssize_t avail;
#define THRESHOLD	64

static inline void resetlog(void) { tail = head = data; avail = 0; }

static inline void storelog(const char *const buf, const size_t len)
{
    if (len > (size_t)(end - tail)) {
	static int be_warned = 0;
	if (!be_warned) {
	    warn("log buffer exceeded");
	    be_warned++;
	}
	goto xout;
    }
    memcpy(tail, buf, len);
    avail = (tail += len) - head;
xout:
    return;
}

static inline void addlog(const char c)
{
    if (end - tail <= 0) {
	static int be_warned = 0;
	if (!be_warned) {
	    warn("log buffer exceeded");
	    be_warned++;
	}
	goto xout;
    }
    *tail = c;
    avail = (tail += 1) - head;
xout:
    return;
}

void writelog(void)
{
    int oldstate;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

    lock(&llock);
    if (!flog) {
	resetlog();
	unlock(&llock);

	pthread_setcancelstate(oldstate, NULL);
	return;
    }
    clearerr(flog);
    while (avail > 0) {
	size_t ret = (size_t)avail;

	if (avail > TRANS_BUFFER_SIZE)
	    ret = TRANS_BUFFER_SIZE;

	if (!flog || nsigsys) {
	    resetlog();
	    break;
	}
	ret = fwrite(head, sizeof(unsigned char), ret, flog);
	if (!ret && ferror(flog)) {
	    resetlog();
	    break;
	}
	head += ret;

	if (head >= tail) {		/* empty, reset buffer */
	    resetlog();
	    break;
	}
	if (head > data) {		/* buffer not empty, move contents */
	    avail = tail - head;
	    head = (unsigned char *)memmove(data, head, avail);
	    tail = head + avail;
	}
    }
    unlock(&llock);
    if (flog) {
	fflush(flog);
	fdatasync(fileno(flog));
    }
    pthread_setcancelstate(oldstate, NULL);
}

void flushlog(void)
{
    if (ljoin.canceled == 0) pthread_cond_broadcast(&lcond);
}

static inline int thread_poll(int msec, mutex_t *outer)
{
    int ret = 1;

    if (avail <= THRESHOLD) {
	struct timeval now;

	if (gettimeofday(&now, NULL) == 0) {
	    struct timespec abstime;
	    int err;

	    now.tv_usec += msec * 1000;
	    while (now.tv_usec >= 1000000) {
		now.tv_sec++;
		now.tv_usec -= 1000000;
	    }
	    abstime.tv_sec  = now.tv_sec;
	    abstime.tv_nsec = now.tv_usec * 1000;

	    do {
		if (outer->canceled == 0) {
		    volatile int locked = outer->locked;
		    outer->locked = 0;
		    err = pthread_cond_timedwait(&lcond, &outer->mutex, &abstime);
		    outer->locked = locked;
		}
	    } while (err == EINTR);

	    if (err == ETIMEDOUT || err == EBUSY)
		ret = 0;
	}
    }

    return ret;
}

/*
 * Remove Escaped sequences and write out result (see
 * linux/drivers/char/console.c in do_con_trol()).
 */
enum {	ESnormal, ESesc, ESsquare, ESgetpars, ESgotpars, ESfunckey,
	EShash, ESsetG0, ESsetG1, ESpercent, ESignore, ESnonstd,
	ESpalette };
#define NPAR 16
static unsigned int state = ESnormal;
static int npar, nl, cr;

/*
 * Workaround for spinner of fsck/e2fsck
 * Uses ascii lines ending with '\r' only
 */
static int spin;

void parselog(const char *buf, const size_t s)
{
    int c;
    ssize_t r = s, up;
    unsigned char uprt[16];
    static int follow;

    lock(&llock);

    while (r > 0) {
	c = (unsigned char)*buf;

	/* Check for first byte of a UTF-8 multibyte sequence */
	if (!follow) {
	    if ((c & 0xe0) == 0xc0) {
		follow = 1;
	    } else if ((c & 0xf0) == 0xe0) {
		follow = 2;
	    } else if ((c & 0xf8) == 0xf0) {
		follow = 3;
	    } else if ((c & 0xfc) == 0xf8) {
		follow = 4;
	    } else if ((c & 0xfe) == 0xfc) {
		follow = 5;
	    } else {
		/* invalid */
		follow = 0;
	    }
	}

	nl = 0;

	switch(state) {
	case ESnormal:
	default:
	    state = ESnormal;
	    switch (c) {
	    case 0  ...  8:
	    case 16 ... 23:
	    case 25:
	    case 28 ... 31:
		follow = 0;
		addlog('^'); addlog(c + 64);
		break;
	    case '\n':
		nl = 1;
		follow = 0;
		cr = spin = 0;
		addlog(c);
		break;
	    case '\r':
		follow = 0;
		if (cr++ > 0)
		{
		    spin++;
#if 0
		    storelog(">spin", 5);
		    addlog(48+spin);
		    addlog('<');
#endif
		}
		break;
	    case 14:
	    case 15:
		/* ^N and ^O used in xterm for rmacs/smacs  *
		 * on console \033[10m and \033[11m is used */
	    case 24:
	    case 26:
		follow = 0;
		break;
	    case '\033':
		follow = 0;
		state = ESesc;
		break;
	    case '\t':
	    case  32 ... 126:
		addlog(c);
		follow = 0;
		break;
	    case 160 ... 255:
		if (follow) {
		    if ((c & 0xc0) == 0x80)
			follow--;
		    addlog(c);
		    break;
		}
		if ((up = snprintf((char*)uprt, sizeof(uprt), "\\%03o", c)) > 0)
		    storelog((char*)uprt, (size_t)up);
		break;
	    case 127:
		follow = 0;
		addlog('^'); addlog('?');
		break;
	    case 128 ... 159:
		if (follow) {
		    if ((c & 0xc0) == 0x80)
			follow--;
		    addlog(c);
		    break;
		}
		if ((up = snprintf((char*)uprt, sizeof(uprt), "\\%03o", c)) > 0)
		    storelog((char*)uprt, (size_t)up);
		break;
	    default:
		cr = 0;
		follow = 0;
		if ((up = snprintf((char*)uprt, sizeof(uprt), "0x%X", c)) > 0)
		    storelog((char*)uprt, (size_t)up);
		break;
	    }
	    break;
	case ESesc:
	    follow = 0;
	    state = ESnormal;
	    switch((unsigned char)c) {
	    case '[':
		state = ESsquare;
		break;
	    case ']':
		state = ESnonstd;
		break;
	    case '%':
		state = ESpercent;
		break;
	    case 'E':
	    case 'D':
		addlog('\n');
		nl = 1;
		break;
	    case '(':
		state = ESsetG0;
		break;
	    case ')':
		state = ESsetG1;
		break;
	    case '#':
		state = EShash;
		break;
#ifdef BLOGD_EXT
	    case '^':				/* Boot log extension */
		state = ESignore;
		break;
#endif
	    default:
		break;
	    }
	    break;
	case ESnonstd:
	    follow = 0;
	    if        (c == 'P') {
		npar = 0;
		state = ESpalette;
	    } else if (c == 'R')
		state = ESnormal;
	    else
		state = ESnormal;
	    break;
	case ESpalette:
	    follow = 0;
	    if ((c>='0'&&c<='9') || (c>='A'&&c<='F') || (c>='a'&&c<='f')) {
		npar++;
		if (npar==7)
		    state = ESnormal;
	    } else
		state = ESnormal;
	    break;
	case ESsquare:
	    follow = 0;
	    npar = 0;
	    state = ESgetpars;
	    if (c == '[') {
		state = ESfunckey;
		break;
	    }
#if 0
	    if (c == 'K')
		storelog(" el ", 4);
#endif
	    if (c == '?')
		break;
	case ESgetpars:
	    follow = 0;
	    if (c==';' && npar<NPAR-1) {
		npar++;
		break;
	    } else if (c>='0' && c<='9') {
		break;
	    } else
		state = ESgotpars;
	case ESgotpars:
	    follow = 0;
	    state = ESnormal;
	    break;
	case ESpercent:
	    follow = 0;
	    state = ESnormal;
	    break;
	case ESfunckey:
	case EShash:
	case ESsetG0:
	case ESsetG1:
	    follow = 0;
	    state = ESnormal;
	    break;
#ifdef BLOGD_EXT
	case ESignore:				/* Boot log extension */
	    follow = 0;
	    state = ESesc;
	    {
		unsigned char echo[64];
		ssize_t len;

		/* Release the lock while doing IO */
		unlock(&llock);
		if ((len = snprintf(echo, sizeof(echo), "\033[%lu;%dR", line, nl)) > 0)
		    safeout(fdread, echo, len, -1);
		else
		    safeout(fdread, "\033R", 2, -1);
		tcdrain(fdread);
		lock(&llock);
	    }
	    break;
#endif
	}
	buf++;
	r--;
    }

    unlock(&llock);
}

void copylog(const char *buf, const size_t s)
{
    lock(&llock);
    if (!nl)
	addlog('\n');
    storelog(buf, s);
    if (buf[s-1] != '\n')
	addlog('\n');
    nl = 1;
    unlock(&llock);
}

/*
 * Dump the kernel messages read from /dev/kmsg to the boot log
 */
void dump_kmsg(FILE *log)
{
    char buf[BUFSIZ];
    ssize_t len;
    int fd;

    fd = open("/dev/kmsg", O_RDONLY|O_NONBLOCK);
    if (fd < 0) {
	warn("can not open /dev/kmsg");
	return;
    }

    lseek(fd, 0, SEEK_DATA);

    do {
	len = read(fd, buf, sizeof(buf)-1);
    } while (len < 0 && errno == EPIPE);
    if (len >= 0)
	buf[len] = '\0';

    while (len > 0) {
	char *p = &buf[0];
	const char *end;
	const char *field[6];
	const char sep[] = ",,,;\n\n";
	int n;

	end = p + (len - 1);
	while (p < end && isspace(*p))
	    p++;

	n = 0;
	field[n] = p;
	while ((p = strchr(p, sep[n++]))) {
	    if (!p || !*p || n > 5)
		break;
	    *p++ = '\0';
	    field[n] = p;
	}

	if (n >= 6) {
	    struct timeval tv;
	    uint64_t usec;
	    char *rest;
	    int saveerr = errno;

	    errno = 0;
	    usec = strtoumax(field[2], &rest, 10);

	    if (!errno) {
		char stamp[128];
		int ret;

		tv.tv_usec = usec % 1000000;
		tv.tv_sec = usec / 1000000;

		ret = snprintf(&stamp[0], sizeof(stamp)-1, "[%5lu.%06lu] ", tv.tv_sec, tv.tv_usec);
		if (ret < 0)
		    warn("snprintf error");
		else {
#define RINGBUF	0
#if RINGBUF
		    stamp[ret] = '\0';
		    lock(&llock);
		    storelog(stamp, (size_t)ret);
		    unlock(&llock);
#else
		    fwrite(&stamp[0], sizeof(char), (size_t)ret, log); 
#endif
		}
	    }
	    errno = saveerr;
#if RINGBUF
	    parselog(field[4], strlen(field[4]));

	    lock(&llock);
	    if (!nl)
	        addlog('\n');
	    unlock(&llock);
#else
	    fwrite(&field[4][0], sizeof(char), strlen(field[4]), log); 
	    fputc('\n', log);
#endif
	}

	do {
	    len = read(fd, buf, sizeof(buf)-1);
	} while (len < 0 && errno == EPIPE);
	if (len >= 0)
	    buf[len] = '\0';
    }

    close(fd);
}

static void *action(void *dummy attribute((unused)))
{
    sigset_t sigset, save_oldset;
    sigemptyset(&sigset);

    sigaddset(&sigset, SIGTTIN);
    sigaddset(&sigset, SIGTTOU);
    sigaddset(&sigset, SIGTSTP);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGQUIT);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGSYS);
    sigaddset(&sigset, SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK, &sigset, &save_oldset);

    lock(&ljoin);
    running = 1;
    while (running) {

	if (!thread_poll(150, &ljoin))
	    continue;

	writelog();
    }
    unlock(&ljoin);

    (void)pthread_sigmask(SIG_SETMASK, &save_oldset, NULL);
    ljoin.used = 0;

    return NULL;
}

void start_logging(void)
{
    struct sched_param param;
    int policy = SCHED_RR;

    if (running || !flog)
	return;

    pthread_getschedparam(pthread_self(), &policy, &param);
    pthread_create(&lthread, NULL, &action, NULL);

    policy = SCHED_RR;
    param.sched_priority = sched_get_priority_max(policy)/2 + 1;
    pthread_setschedparam(pthread_self(), policy, &param);
    pthread_setschedparam(lthread, policy, &param);
}

void stop_logging(void)
{
    if (!running)
	return;

    lock(&ljoin);
    running = 0;
    unlock(&ljoin);
    ljoin.canceled = 1;
    pthread_cond_broadcast(&lcond);
    sched_yield();
    if (ljoin.used && lthread)
	pthread_cancel(lthread);
}

FILE *open_logging(int fd)
{
    lock(&llock);
    flog = fdopen(fd, "a");
    if (!flog) {
	unlock(&llock);
	error("Can not open boot loggin file");
    }
    unlock(&llock);

    return flog;
}

FILE *close_logging(void)
{
    if (!flog)
	return NULL;

    writelog();

    if (!nl)
	fputc('\n', flog);

    lock(&llock);
    (void)fclose(flog);
    flog = NULL;
    unlock(&llock);

    return flog;
}

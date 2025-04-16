/*
 * listing.h
 *
 * Copyright 2000 Werner Fink, 2000 SuSE GmbH Nuernberg, Germany.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#ifndef  TRANS_BUFFER_SIZE
# define TRANS_BUFFER_SIZE	4096
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
# ifndef  inline
#  define inline		__inline__
# endif
# ifndef  restrict
#  define restrict		__restrict__
# endif
# ifndef  volatile
#  define volatile		__volatile__
# endif
# ifndef  asm
#  define asm			__asm__
# endif
# ifndef  extension
#  define extension		__extension__
# endif
# ifndef  typeof
#  define typeof		__typeof__
# endif
#endif
#ifndef  attribute
# define attribute(attr)	__attribute__(attr)
#endif
#include "listing.h"

#define alignof(type)		((sizeof(type)+(sizeof(void*)-1)) & ~(sizeof(void*)-1))
#define strsize(string)		((strlen(string)+1)*sizeof(char))

#if defined __USE_ISOC99
#  define _cat_pragma(exp)	_Pragma(#exp)
#  define _weak_pragma(exp)	_cat_pragma(weak name)
#else
#  define _weak_pragma(exp)
#endif

#define _declare(name)		__extension__ extern __typeof__(name) name
#define weak_symbol(name)	_weak_pragma(name) _declare(name) __attribute__((weak))

/* external function in program */
extern void error (const char *fmt, ...);

/* console.c */

#define PLYMOUTH_SOCKET_PATH	"\0/org/freedesktop/plymouthd"
#define ANSWER_TYP		"\x2"
#define ANSWER_ENQ		"\x5"
#define ANSWER_ACK		"\x6"
#define ANSWER_MLT		"\t"
#define ANSWER_NCK		"\x15"

#define MAGIC_PRG_STOP		'A'
#define MAGIC_PRG_CONT		'a'
#define MAGIC_UPDATE		'U'
#define MAGIC_SYS_UPDATE	'u'
#define MAGIC_SYS_INIT		'S'
#define MAGIC_DEACTIVATE	'D'
#define MAGIC_REACTIVATE	'r'
#define MAGIC_SHOW_SPLASH	'$'
#define MAGIC_HIDE_SPLASH	'H'
#define MAGIC_CHMOD		'C'
#define MAGIC_CHROOT		'R'
#define MAGIC_ACTIVE_VT		'V'
#define MAGIC_QUESTION		'W'
#define MAGIC_SHOW_MSG		'M'
#define MAGIC_HIDE_MSG		'm'
#define MAGIC_KEYSTROKE		'K'
#define MAGIC_KEYSTROKE_RM	'L'
#define MAGIC_PING		'P'
#define MAGIC_QUIT		'Q'
#define MAGIC_CLOSE		'X'	/* Not known by plymouthd, but blogd does close log file */
#define MAGIC_FINAL		'F'	/* Not known by plymouthd, but blogd move log to old log */
#define MAGIC_CACHED_PWD	'c'
#define MAGIC_ASK_PWD		'*'
#define MAGIC_DETAILS		'!'	/* blogd does always spool log messages */

struct console {
    list_t node;
    char *tty;
    dev_t dev;
    pid_t pid;
    int flags;
    int fd, tlock;
    ssize_t max_canon;
    struct termios ltio, otio, ctio;
};

#define CON_PRINTBUFFER	(1)
#define CON_CONSDEV	(2) /* Last on the command line */
#define CON_ENABLED	(4)
#define CON_BOOT	(8)
#define CON_ANYTIME	(16) /* Safe to call when cpu is offline */
#define CON_BRL		(32) /* Used for a braille device */
#define CON_SERIAL	(64) /* Serial line */

extern sigset_t omask;
extern int final;
extern int epfd;
extern int evmax;
extern volatile sig_atomic_t signaled;
extern volatile sig_atomic_t nsigsys;
extern volatile sig_atomic_t asking;

extern void remember_arg0(volatile char *arg0);
extern ssize_t safein (int fd, void *ptr, size_t s);
extern void safeout (int fd, const void *ptr, size_t s, ssize_t max);

extern void prepareIO(void (*rfunc)(int), const int listen, const int in);
extern void safeIO (void);
extern void closeIO(void);

extern struct console *cons;
extern void getconsoles(struct console **cons, int io);

/* chroot.c */
extern void new_root(const char *root);

/* devices.c */
extern char *charname(const char *dev);
extern char *chardev(const dev_t cons);

/* dir.c */
extern void pushd(const char * path);
extern void popd(void);

/* epoll.c */
extern void epoll_addread(int fd, void *fptr);
extern void epoll_answer_once(int fd, void *fptr);
extern void epoll_delete(int fd);
extern void (*epoll_handle(void *ptr, int *fd))(int);
extern void epoll_close_fd(void);

/* frobnicate.c */
extern void *frobnicate(void *in, const size_t len);

/* io.c */
extern int can_read(int fd, const long timeout);
extern int can_write(int fd, const int timeout);
extern void clear_input(int fd);

/* log.c */
extern volatile sig_atomic_t nsigsys;
extern void writelog(void);
extern void flushlog(void);
extern void parselog(const char *buf, const size_t s);
extern void copylog(const char *buf, const size_t s);
extern void dump_kmsg(FILE *log);
extern void start_logging(void);
extern void stop_logging(void);
extern FILE *open_logging(int fd);
extern FILE *close_logging(void);

/* proc.c */
extern char *proc2exe(const pid_t pid);
extern void list_fd(const pid_t pid);

/* readpw.c */
extern ssize_t readpw(int fd, char *pass, int eightbit);

/* shm.c */
extern void* shm_malloc(size_t size);

/* signals.c */
extern void set_signal(int sig, struct sigaction *old, sighandler_t handler);
extern void reset_signal(int sig, struct sigaction *old);
extern int restart_sig(int sig, int flag);

/* socket.c */
extern int open_un_socket_and_listen(void);
extern int open_un_socket_and_connect(void);

/* strings.c */
extern void str0append(char **buf, size_t *size, const char *str);

/* tty.c */
extern int open_tty(const char *name, int mode);
extern int request_tty(const char *tty);

#define MAX_PASSLEN	LINE_MAX
/* Some shorthands for control characters. */
#define CTL(x)		((x) ^ 0100)	/* Assumes ASCII dialect */
#define CR		CTL('M')	/* carriage return */
#define NL		CTL('J')	/* line feed */
#define BS		CTL('H')	/* back space */
#define DEL		CTL('?')	/* delete */

/* Defaults for line-editing etc. characters; you may want to change these. */
#define DEF_ERASE	DEL		/* default erase character */
#define DEF_INTR	CTL('C')	/* default interrupt character */
#define DEF_QUIT	CTL('\\')	/* default quit char */
#define DEF_KILL	CTL('U')	/* default kill char */
#define DEF_EOF		CTL('D')	/* default EOF char */
#define DEF_EOL		0
#define DEF_SWITCH	0		/* default switch char */

#define UL_TTY_KEEPCFLAGS	(1 << 1)
#define UL_TTY_UTF8		(1 << 2)

static inline void reset_virtual_console(struct termios *tp, int flags)
{
    /* Use defaults of <sys/ttydefaults.h> for base settings */
    tp->c_iflag |= TTYDEF_IFLAG;
    tp->c_oflag |= TTYDEF_OFLAG;
    tp->c_lflag |= TTYDEF_LFLAG;

    if ((flags & UL_TTY_KEEPCFLAGS) == 0) {
#ifdef CBAUD
	tp->c_lflag &= ~CBAUD;
#endif
	tp->c_cflag |= (B38400 | TTYDEF_CFLAG);
    }

    /* Sane setting, allow eight bit characters, no carriage return delay
     * the same result as `stty sane cr0 pass8'
     */
    tp->c_iflag |=  (ICRNL | IXON);
    tp->c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP| \
		     INLCR | IGNCR | IXOFF | IUCLC | IXANY | IMAXBEL);
    tp->c_oflag |=  (OPOST | ONLCR | NL0 | CR0 | TAB0 | BS0 | VT0 | FF0);
    tp->c_oflag &= ~(OLCUC | OCRNL | ONOCR | ONLRET | OFILL | \
    		     NLDLY|CRDLY|TABDLY|BSDLY|VTDLY|FFDLY);
    tp->c_lflag |=  (ISIG | ICANON | IEXTEN | ECHO|ECHOE|ECHOK|ECHOKE);
    tp->c_lflag &= ~(ECHONL|ECHOPRT | NOFLSH | TOSTOP | FLUSHO | EXTPROC | ECHOCTL);

    if ((flags & UL_TTY_KEEPCFLAGS) == 0) {
	tp->c_cflag |=  (CREAD | CS8 | HUPCL);
	tp->c_cflag &= ~(PARODD | PARENB | CMSPAR | CLOCAL | CRTSCTS);
    }
#ifdef OFDEL
    tp->c_oflag &= ~OFDEL;
#endif
#ifdef XCASE
    tp->c_lflag &= ~XCASE;
#endif
#ifdef IUTF8
    if (flags & UL_TTY_UTF8)
	tp->c_iflag |= IUTF8;	    /* Set UTF-8 input flag */
    else
	tp->c_iflag &= ~IUTF8;
#endif
    /* VTIME and VMIN can overlap with VEOF and VEOL since they are
     * only used for non-canonical mode. We just set the at the
     * beginning, so nothing bad should happen.
     */
    tp->c_cc[VTIME]    = 0;
    tp->c_cc[VMIN]     = 1;
    tp->c_cc[VINTR]    = CINTR;
    tp->c_cc[VQUIT]    = CQUIT;
    tp->c_cc[VERASE]   = CERASE; /* ASCII DEL (0177) */
    tp->c_cc[VKILL]    = CKILL;
    tp->c_cc[VEOF]     = CEOF;
#ifdef VSWTC
    tp->c_cc[VSWTC]    = _POSIX_VDISABLE;
#elif defined(VSWTCH)
    tp->c_cc[VSWTCH]   = _POSIX_VDISABLE;
#endif
    tp->c_cc[VSTART]   = CSTART;
    tp->c_cc[VSTOP]    = CSTOP;
    tp->c_cc[VSUSP]    = CSUSP;
    tp->c_cc[VEOL]     = _POSIX_VDISABLE;
    tp->c_cc[VREPRINT] = CREPRINT;
    tp->c_cc[VDISCARD] = CDISCARD;
    tp->c_cc[VWERASE]  = CWERASE;
    tp->c_cc[VLNEXT]   = CLNEXT;
    tp->c_cc[VEOL2]    = _POSIX_VDISABLE;
}

/*
 * readpw.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/ttydefaults.h>
#include <unistd.h>
#include "libconsole.h"

struct chardata {
    int erase;		/* erase character */
    int kill;		/* kill character */
    int eol;		/* end-of-line character */
    int parity;		/* what parity did we see */
    int capslock;	/* upper case without lower case */
};

ssize_t readpw(int fd, char *pass, int eightbit)
{
    char *ptr = pass;
    struct chardata cp;
    int ret, rcw;

    cp.eol = *ptr = '\0'; 

    while (cp.eol == '\0') {
	char ascval, c;

	ret = read(fd, &c, 1);
	if (ret < 0) {
	    if (errno == EINTR || errno == EAGAIN) {
		usleep(250000);
		continue;
	    }
	    switch (errno) {
	    case 0:
	    case EIO:
	    case ESRCH:
	    case EINVAL:
	    case ENOENT:
		break;
	    default:
		warn("cannot read passphrase");
		break;
	    }
	    return -1;
	}

	if (eightbit)
	    ascval = c;
	else if (c != (ascval = (c & 0177))) {
	    uint8_t bits, mask;
	    for (bits = 1, mask = 1; mask & 0177; mask <<= 1) {
		if (mask & ascval)
		    bits++;
	    }
	    cp.parity |= ((bits & 1) ? 1 : 2);
	}

	switch (ascval) {
	case 0:
	    *ptr = '\0';
	    return 0;
	case CR:
	case NL:
	    *ptr = '\0';
	    cp.eol = ascval;
	    break;
	case BS:
	case CERASE:
	    cp.erase = ascval;
	    if (ptr > pass)
		ptr--;
	    break;
	case CKILL:
	    cp.kill = ascval;
	    while (ptr > pass)
		ptr--;
	    break;
	case CEOF:
	    return 0;
	default:
	    if ((size_t)(ptr - pass) >= (MAX_PASSLEN-1)) {
		errno = EOVERFLOW;
		return -1;
	    }
	    *ptr++ = ascval;
	    do {
		rcw = write(fd, "*", 1);
	    } while(rcw == -1 && errno == EINTR);
	    break;
	}
    }

    return (ssize_t)(ptr - pass);
}

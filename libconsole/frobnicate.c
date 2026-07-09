/*
 * frobnicate.c
 *
 * In-memory reversible obfuscation for cached passwords.
 * This is not cryptographic protection against a memory-reading attacker.
 * It is only meant to avoid trivial/plaintext exposure in memory.
 *
 * Copyright 2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define FROBMASK_LEN	32

static unsigned char frobmask[FROBMASK_LEN];

static void initialseed(void) __attribute__((__constructor__));
static void initialseed(void)
{
    size_t off = 0;
 
    while (off < sizeof(frobmask)) {
	ssize_t ret = getrandom(frobmask+off, sizeof(frobmask)-off, GRND_NONBLOCK);
	if (ret < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	if (ret == 0)
	    break;
	off += (size_t)ret;
    }
 
    if (off == sizeof(frobmask))
	return;

    /*
     * Early-boot fallback:
     * weak but stable for the process lifetime and inherited across fork().
     */
    {
	struct timeval tv;
	unsigned int seed;
	size_t pos;

	gettimeofday(&tv, NULL);
	seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec ^ getpid());
 
	for (pos = 0; pos < sizeof(frobmask); pos++) {
	    seed = seed * 1103515245u + 12345u;
	    frobmask[pos] = (unsigned char)((seed >> 16) & 0xff);
	}
 
	/* Avoid an all-zero mask, just in case */
	for (pos = 0; pos < sizeof(frobmask); pos++) {
	    if (frobmask[pos] != 0)
		return;
	}

	frobmask[0] = 42;
    }
}

void *frobnicate(void *in, const size_t len)
{
    unsigned char *ptr = (unsigned char *)in;
    size_t pos;

    if (!ptr || len == 0)
	return in;

    for (pos = 0; pos < len; pos++)
	ptr[pos] ^= frobmask[pos % FROBMASK_LEN];

    return in;
}

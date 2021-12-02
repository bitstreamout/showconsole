/*
 * blogger.c
 *
 * Copyright 2000 Werner Fink, 2000 SuSE GmbH Nuernberg, Germany.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef  _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "libblogger.h"

volatile char *arg0;

int main(int argc, char * argv[])
{
    int c, lvl = 'n';

    arg0 = (volatile char*)argv[0];

    while ((c = getopt(argc, argv, "ndfsu")) != -1) {
	switch (c) {
	case B_NOTICE:
	case B_DONE:
	case B_FAILED:
	case B_SKIPPED:
	case B_UNUSED:
	    lvl = c;
	    break;
	case '?':
	default:
	    lvl = B_NOTICE;
	    break;
	}
    }
    argv += optind;
    argc -= optind;

    if (!argc)
	exit(0);

    c = argc;
    if (bootlog(lvl, argv[0]) < 0)
	exit(0);

    argv++;
    argc--;

    for (c = 0; c < argc; c++)
	bootlog(-1, " %s", argv[c]);
    bootlog(-1, "\n");

    return 0;
}

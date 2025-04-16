/*
 * devices.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include "libconsole.h"

static dev_t dev;
static char *name;
static int find_chardevice(const char *fpath, const struct stat *st,
    int typeflag, struct FTW *ftwbuf)
{
    if (typeflag == FTW_NS)
	return FTW_CONTINUE;
    if (typeflag == FTW_D)
	return FTW_CONTINUE;
    if (typeflag == FTW_SLN)
	return FTW_CONTINUE;
    if (typeflag == FTW_SL)
	return FTW_CONTINUE;
    if (typeflag == FTW_DNR)
	return FTW_CONTINUE;

    if (!S_ISCHR(st->st_mode))
	return FTW_CONTINUE;
    if (dev != st->st_rdev)
	return FTW_CONTINUE;

    name = strdup(fpath);
    if (!name)
	error("can not allocate string");

    return FTW_STOP;
}

char *charname(const char *str)
{
    unsigned int maj, min;
    int ret = 0;

    if (!str || !*str)
	error("no device provided");

    ret = sscanf(str, "%u:%u", &maj, &min);
    if (ret != 2)
	error("can not scan %s: %m", str);
    dev = makedev(maj, min);

    if (nftw("/dev", find_chardevice, 10, FTW_PHYS) < 0)
	error("can not follow tree below /dev: %m");

    return name;
}

char *chardev(const dev_t cons)
{
    dev = cons;

    if (nftw("/dev", find_chardevice, 10, FTW_PHYS) < 0)
	error("can not follow tree below /dev: %m");

    return name;
}

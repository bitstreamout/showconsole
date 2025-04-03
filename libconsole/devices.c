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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include "libconsole.h"

char *charname(const char *str)
{
    DIR *dir;
    char *name;
    struct dirent *d;
    unsigned int maj, min;
    int fd, ret = 0;
    dev_t dev; 

    if (!str || !*str)
	error("no device provided");

    ret = sscanf(str, "%u:%u", &maj, &min);
    if (ret != 2)
	error("can not scan %s", str);
    dev = makedev(maj, min);

#if defined(__s390__)
    if (maj == 227) {
	dir = opendir("/dev/3270");
	if (!dir)
	    error("can not open /dev/3270");
    } else
#endif
    {
	dir = opendir("/dev");
	if (!dir)
	    error("can not open /dev");
    }

    fd = dirfd(dir);
    rewinddir(dir);

    name = NULL;
    while ((d = readdir(dir))) {
	    struct stat st;
	    char path[PATH_MAX+1];
    
	    if (*d->d_name == '.')
		continue;

	    if (fstatat(fd, d->d_name, &st, 0) < 0)
		continue;

	    if (!S_ISCHR(st.st_mode))
		continue;

	    if (dev != st.st_rdev)
		continue;

	    if ((size_t)snprintf(path, sizeof(path), "/dev/%s", d->d_name) >= sizeof(path)) {
		errno = EOVERFLOW;
		error("can not handle %s", d->d_name);
		break;
	    }

	    name = realpath(path, NULL);
	    break;
    }

    closedir(dir);

    return name;
}

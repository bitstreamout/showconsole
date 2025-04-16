/*
 * shm.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <fcntl.h>     
#include <linux/magic.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statfs.h>
#include <unistd.h>
#include "libconsole.h"

/*
 * glibc does not provide a shm_mkstemp(char *template) and not
 * using shm_open() but hard coded /dev/shm seems to by risky,
 * therefore determine the location for POSIX shared memory.
 */

static const char *devshm;

static void _locateshm(void) __attribute__((__constructor__));
static void _locateshm(void)
{
    static const char defaultdir[] = "/dev/shm";
    struct statfs st;
    struct mntent *p;
    FILE *mounts;
    int ret;

    ret = statfs(defaultdir, &st);
    if (ret == 0 && (st.f_type == RAMFS_MAGIC || st.f_type == TMPFS_MAGIC)) {
	devshm = &defaultdir[0];
	return;
    }

    mounts = setmntent("/proc/mounts", "re");
    if (!mounts)
	return;		/* Ouch! */

    while((p = getmntent(mounts))) {

	if (strcmp(p->mnt_type, "tmpfs") != 0)
	    continue;

	if (strlen(p->mnt_dir) <= 0)
	    continue;

	ret = statfs(p->mnt_dir, &st);
	if (ret < 0)
	    continue;

	if (st.f_type != RAMFS_MAGIC && st.f_type != TMPFS_MAGIC)
	    continue;

	devshm = strdup(p->mnt_dir);
	break;
    }

    endmntent(mounts);
}

void* shm_malloc(size_t size)
{
    void *area;
    char *template;
    int shmfd = -1;

    if (devshm) {
	int ret;

	ret = asprintf(&template, "%s/blogd-XXXXXX", devshm);
	if (ret < 0)
	    error("can not allocate string for shared memory area");

	shmfd = mkstemp(template);
	if (ret < shmfd)
	    error("can not generate shared memory area");

	ret = ftruncate(shmfd, size);
	if (ret < 0)
	    error("can not allocate shared memory object");
    }

    area = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_LOCKED|MAP_SHARED|MAP_ANONYMOUS, shmfd, 0);
    if (area == MAP_FAILED)
	 error("can not map shared memory object into memory");

    if (devshm) {
	/* shm_ */ unlink(template);
	free(template);
    }

    return area;
}

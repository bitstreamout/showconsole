/*
 * dir.c
 *
 * Copyright 2000,2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <unistd.h>
#include "listing.h"
#include "libconsole.h"

/*
 * push and popd directory changes
 */

typedef struct pwd_struct {
    list_t      deep;
    char        *pwd;
} pwd_t;
#define getpwd(list)	list_entry((list), struct pwd_struct, deep)
static list_t pwd = { &(pwd), &(pwd) }, *topd = &(pwd);

void pushd(const char * path)
{
    pwd_t *  dir;

    dir = (pwd_t *)malloc(sizeof(pwd_t));
    if (dir) {
	if (!(dir->pwd = getcwd(NULL, 0)))
	    goto err;
	insert(&(dir->deep), topd->prev);
	goto out;
    }
err:
    error("%m");
out:
    if (chdir(path) < 0)
	error ("pushd() can not change to directory %s", path);
}

void popd(void)
{
    list_t * tail = topd->prev;
    pwd_t *  dir;

    if (list_empty(topd))
	goto out;
    dir = getpwd(tail);
    if (chdir(dir->pwd) < 0)
	error ("popd() can not change directory %s", dir->pwd);
    free(dir->pwd);
    delete(tail);
    free(dir);
out:
    return;
}

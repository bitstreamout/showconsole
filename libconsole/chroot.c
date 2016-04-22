/*
 * chroot.c
 *
 * Copyright 2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <unistd.h>
#include "libconsole.h"

void new_root(const char *root)
{
    int ret;
    ret = chdir(root);
    if (ret < 0)
	error("can change to working directory %s", root);
    ret = chroot(".");
    if (ret < 0)
	error("can change root directory");
    ret = chdir("/");
    if (ret < 0)
	error("can change to working directory /");
}

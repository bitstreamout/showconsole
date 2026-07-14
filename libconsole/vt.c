/*
 * vt.c - Virtual Terminal Management for blogd
 *
 * Copyright 2026 Werner Fink
 * Copyright 2026 SUSE Software Solutions Germany GmbH
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include "libconsole.h"

#if !defined(__s390__) && !defined(__s390x__)
/* Check if the system supports VTs (e.g. not s390x) */
int vt_supported(void)
{
    return (access("/sys/devices/virtual/tty/tty0/active", R_OK) == 0);
}

/* Checks if the terminal is running within a graphical environment (X11/Wayland) */
int vt_is_graphics(int fd)
{
    int mode = KD_TEXT;
    if (ioctl(fd, KDGETMODE, &mode) < 0)
        return 0;
    return (mode == KD_GRAPHICS);
}

/* Checks if the terminal is a pure text console (TTY) */
int vt_is_text(int fd)
{
    int mode = KD_TEXT;
    if (ioctl(fd, KDGETMODE, &mode) < 0)
        return 0;
    return (mode == KD_TEXT);
}

/* Get current KD mode (KD_TEXT or KD_GRAPHICS) */
int vt_get_mode(int fd)
{
    int mode = KD_TEXT;
    if (ioctl(fd, KDGETMODE, &mode) < 0)
	return -1;
    return mode;
}

/* Set KD mode to KD_TEXT */
int vt_set_text_mode(int fd)
{
    int mode = KD_TEXT;
    if (ioctl(fd, KDSETMODE, &mode) < 0)
	return -1;
    return mode;
}

/* Find the first free Virtual Terminal */
int vt_get_free(int fd)
{
    int vtno = -1;
    if (ioctl(fd, VT_OPENQRY, &vtno) < 0)
	return -1;
    return vtno;
}

/* Get currently active VT number */
int vt_get_active(int fd)
{
    struct vt_stat vstate;
    if (ioctl(fd, VT_GETSTATE, &vstate) < 0)
	return -1;
    return vstate.v_active;
}

/* Switch to a specific VT and wait until the switch completes */
int vt_switch(int fd, int vtno)
{
    if (ioctl(fd, VT_ACTIVATE, vtno) < 0)
	return -1;
    if (ioctl(fd, VT_WAITACTIVE, vtno) < 0)
	return -1;
    return 0;
}

/* Deallocate a VT after use to free up kernel memory */
void vt_disallocate(int fd, int vtno)
{
    ioctl(fd, VT_DISALLOCATE, vtno);
}
#endif

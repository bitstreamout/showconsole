/*
 * vmcp.c
 *
 * Based on:
 *
 * Copyright IBM Corp. 2018
 * s390-tools is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "libconsole.h"

#if defined(__s390__) || defined(__s390x__)

#define	VMCP_DEVICE_NODE	"/dev/vmcp"
#define	VMCP_GETSIZE		_IOR(0x10, 3, int)
#define	VMCP_SETBUF		_IOW(0x10, 2, int)
#define	VMCP_GETCODE		_IOR(0x10, 1, int)

int openvmcp(void)
{
    return open(VMCP_DEVICE_NODE, O_RDWR|O_NOCTTY);
}

char* queryterm(int fd)
{
    const char* question = "QUERY TERMINAL";
    long pagesize = sysconf(_SC_PAGESIZE);
    int rc = 0, num, buffersize;
    char* ret = NULL;

    num = (strlen(question) + pagesize - 1)/pagesize;
    buffersize = num * pagesize;

    if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
        goto out;
    do {
	rc = write(fd, question, strlen(question));
        if (rc < 0) {
	    if (errno != EINTR)
		goto out;
        }
    } while (rc < 0);
    if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
        goto out;
    if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1)
        goto out;
    ret = (char*)malloc(buffersize);
    if (!ret)
        goto out;
    do {
	rc = read(fd, ret, buffersize);
        if (rc < 0) {
	    if (errno != EINTR)
		goto out;
        }
    } while (rc < 0);
out:
    return ret;
}

int setterm(int fd)
{
    const char* instruction = "TERMINAL MORE 0 0 HOLD OFF";
    long pagesize = sysconf(_SC_PAGESIZE);
    int rc = 0, num, buffersize;
    int ret = -1;

    num = (strlen(instruction) + pagesize - 1)/pagesize;
    buffersize = num * pagesize;

    if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
        goto out;
    do {
	rc = write(fd, instruction, strlen(instruction));
        if (rc < 0) {
	    if (errno != EINTR)
		goto out;
        }
    } while (rc < 0);
    if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
        goto out;
    if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1)
        goto out;
    if (rc == 0 && buffersize == 0)
	ret = 0;
out:
    return ret;
}

static char *more, *hold;

int restoreterm(int fd)
{
    char* instruction;
    long pagesize = sysconf(_SC_PAGESIZE);
    int rc = 0, num, buffersize;
    int ret = -1;

    if (!more || !hold)
	goto out;
    if (asprintf(&instruction, "TERMINAL %s %s", more, hold) == -1)
	goto out;

    num = (strlen(instruction) + pagesize - 1)/pagesize;
    buffersize = num * pagesize;

    if (ioctl(fd, VMCP_SETBUF, &buffersize) == -1)
        goto out;
    do {
	rc = write(fd, instruction, strlen(instruction));
        if (rc < 0) {
	    if (errno != EINTR)
		goto out;
        }
    } while (rc < 0);
    free(instruction);
    if (ioctl(fd, VMCP_GETCODE, &rc) == -1)
        goto out;
    if (ioctl(fd, VMCP_GETSIZE, &buffersize) == -1)
        goto out;
    if (rc == 0 && buffersize == 0)
	ret = 0;
out:
    return ret;
}

void parseterm(char *msg)
{
    int n;
    char *token, *ptr;
    for (n = 1, ptr = msg; ; n++, ptr = NULL) {
	token = strtok(ptr, ",\n");
	if (!token)
	    break;
	if (hold && more)
	    break;
	while (*token == ' ')
	    token++;
	if (strncmp("MORE ", token, 5) == 0)
	    more = strdup(token);
	if (strncmp("HOLD ", token, 5) == 0)
	    hold = strdup(token);
    }
}
#endif

/*
 * proc.c
 *
 * Copyright 2015 Werner Fink, 2015 SuSE Linux GmbH.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "listing.h"
#include "libconsole.h"

/*
 * Get real path of the binary of a specifiy pid.
 */
char *proc2exe(const pid_t pid)
{
    char *tmp, *exe;
    int ret;

    ret = asprintf(&exe, "/proc/%lu/exe", (unsigned long)pid);
    if (ret < 0)
	error("can not allocate string for /proc/%lu/exe", (unsigned long)pid);

    tmp = exe;
    exe = realpath(exe, NULL);
    if (!exe) {
	if (errno != ENOENT && errno != ENOTDIR)
	    error("can not allocate string for /proc/%lu/exe", (unsigned long)pid);
    }
    free(tmp);

    return exe;
}

/*
 * For debugging: show open file descriptors
 */
void list_fd(const pid_t pid)
{
    struct dirent *dr;
    char *fds;
    DIR *dir;
    int ret;

    ret = asprintf(&fds, "/proc/%lu/fd", (unsigned long)pid);
    if (ret < 0)
	error("can not allocate string for /proc/%lu/fd", (unsigned long)pid);

    dir = opendir(fds);
    if (!dir) {
	warn("can not open %s", fds);
	return;
    }
    free(fds);

    while ((dr = readdir(dir))) {
	char tmp[LINE_MAX+1];
	ssize_t len;
	if (dr->d_name[0] == '.')
	    continue;
	len = readlinkat(dirfd(dir), dr->d_name, &tmp[0], LINE_MAX);
	tmp[len] = '\0';
	fprintf(stderr, "/proc/%d/fd/%s %s\n", (int)pid, dr->d_name, tmp);
    }
    closedir(dir);
}

/*
 * Parse kernel command line for our entries
 */
static list_t lparameter = { &(lparameter), &(lparameter) };
typedef struct parameter_s {
    list_t	handle;
    char	*val;
    char	*key;
} parameter_t;
static parameter_t *parameter = (parameter_t*)0;

void parse_cmdline(void) {
    char buf[4096];
    list_t *head;
    FILE *fp;

#ifdef DEBUG_PROC
    fp = fopen("cmdline", "r");
#else
    fp = fopen("/proc/cmdline", "r");
#endif
    if (!fp)
	return;

    if (fgets(buf, sizeof(buf), fp)) {
	char *saveptr;
	char *token = strtok_r(buf, " \n", &saveptr);

	while (token) {
	    const char *prefix = "blog.";
	    const size_t plen = strlen(prefix);

	    if (strncmp(token, prefix , plen) == 0) {
		char *kv = token + plen;
		char *eq = strchr(kv, '=');

		if (eq) {
		    parameter_t *pm;
		    int exists = 0;

		    *eq = '\0';
		    char *key = kv;
		    char *val = eq + 1;

                    if (*val == '\0' || *val == ' ') {
                        *eq = '='; /* restore token for strtok_r */
                        token = strtok_r(NULL, " \n", &saveptr);
                        continue;
                    }

		    list_for_each_entry(pm, &lparameter, handle) {
			if (strcmp(pm->key, key) == 0) {
			    free(pm->val);
			    pm->val = strdup(val);
			    exists++;
			}
		    }
		    if (!exists) {
			if (posix_memalign((void**)&pm, sizeof(void*), alignof(parameter_t)+strlen(key)+1) != 0 || !pm)
			    error("memory allocation");
			pm->key = ((char*)pm)+alignof(parameter_t);
			strcpy(pm->key, key);
			pm->val = strdup(val);

			if (!parameter) {
			    head = &lparameter;
			    parameter = (parameter_t *)head;
			} else
			    head = &(parameter)->handle;
			insert(&pm->handle, head);
		    }
		    *eq = '='; /* restore token for strtok_r */
		}
	    }
	    token = strtok_r(NULL, " \n", &saveptr);
	}
    }
    fclose(fp);
}

char* value_cmdline(const char* key) {
    parameter_t *pm;
    list_for_each_entry(pm, &lparameter, handle) {
	if (strcmp(pm->key, key) == 0) {
            return pm->val;
        }
    }
    return (char*)0;
}

void free_cmdline(void) {
    parameter_t *pm, *n;
    list_for_each_entry_safe(pm, n, &lparameter, handle) {
        free(pm->val);
        delete(&pm->handle);
        free(pm);
    }
}
#ifdef DEBUG_PROC
static int _isinteger(const char *str)
{
    int errs = errno, ret = 1;
    char *endptr;

    errno = 0;
    long attribute((unused)) val = strtol(str, &endptr, 10);

    /* Check if no digits were found at all */
    if (str == endptr)
        ret = 0;

    /* Check for overflow or underflow */
    if (errno == ERANGE)
        ret = 0;

    /* Check for trailing non-numeric characters (e.g., "123abc") */
    if (*endptr != '\0')
        ret = 0;

    errno = errs;
    return ret;
}
int main(void)
{
    parameter_t *pm;
    char *key;
    parse_cmdline();
    list_for_each_entry(pm, &lparameter, handle) {
        printf("key %s = val %s\n", pm->key, pm->val);
    }
    key = value_cmdline("timeout");
    if (key)
        printf("%s %d\n", key, _isinteger(key));
}
#endif

/*
 * coldstart.c
 *
 * Copyright 2026 Werner Fink
 * Copyright 2026 SUSE Software Solutions Germany GmbH
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef  _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "listing.h"
#include "libconsole.h"

extern void parse_ask_file(const char *);

#define MAX_LINE 1024
#define ASK_DIR "/run/systemd/ask-password"

void send_response_to_systemd(const char *socket_path, const char *password)
{
    char *packet;
    size_t pwd_len, packet_len;
    struct sockaddr_un addr;

    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0)
	return;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    /* systemd protocol requires: '+' + password (the \0 is optional) */
    pwd_len = strlen(password);
    packet_len = pwd_len + 1; /* +1 for '+' */
    packet = (char*)malloc(packet_len);

    if (!packet)
	error("memory allocation: %m");

    packet[0] = '+';
    memcpy(packet + 1, password, pwd_len);

    /* Now send the packet as one datagramm (SOCK_DGRAM) */
    sendto(sock, packet, packet_len, 0, (struct sockaddr *)&addr, sizeof(addr));

    free(packet);
    close(sock);
}



/* Inintialize head node for systemd password queries */
list_t pwd_requests = { &(pwd_requests), &(pwd_requests) };

/*
 * Data structure for single password query, compare
 * with https://systemd.io/PASSWORD_AGENTS/
 */
struct request {
    list_t node;
    time_t notafter;
    pid_t pid;
    char *socket_path;
    char *message;
};

struct request *requests = NULL;

/* Helper to safely strip newlines and trailing whitespace */
static void trim_trailing(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ')) {
	str[len - 1] = '\0';
	len--;
    }
}

/* Parses a single ask.<id> file and adds it to the list if valid */
void parse_ask_file(const char *filepath)
{
    char line[MAX_LINE];
    int in_ask_section;
    struct request *req;
    list_t *head;

    FILE *fp = fopen(filepath, "r");
    if (!fp)
	return;

    in_ask_section = 0;

    /* Allocate memory for our custom wrapper structure */
    if (posix_memalign((void**)&req, sizeof(void*), align_up(struct request, void*)) != 0 || !req) {
	fclose(fp);
	error("memory allocation: %m");
    }
    memset(req, 0, align_up(struct request, void*));

    while (fgets(line, sizeof(line), fp)) {
	char *eq, *key, *value;

	trim_trailing(line);

	/* Skip empty lines or comments */
	if (line[0] == '\0' || line[0] == '#')
	    continue;

	/* Section header matching */
	if (line[0] == '[' && line[strlen(line) - 1] == ']') {
	    if (strcmp(line, "[Ask]") == 0) {
		in_ask_section = 1;
	    } else {
		in_ask_section = 0; /* Left the [Ask] block */
	    }
	    continue;		    /* Next line please */
	}

	if (!in_ask_section)
	    continue;		    /* Only parse key=value variables if inside the [Ask] section */

	eq = strchr(line, '=');
	if (!eq)
	    continue;

	*eq = '\0';
	key = line;
	value = eq + 1;

	if (strcmp(key, "Message") == 0)
	    req->message = strdup(value);
	else if (strcmp(key, "PID") == 0)
	    req->pid = (pid_t)atol(value);
	else if (strcmp(key, "Socket") == 0)
	    req->socket_path = strdup(value);
	else if (strcmp(key, "NotAfter") == 0)
	    req->notafter = atoll(value);
    }
    fclose(fp);

    /* If we successfully found at least a message, commit it to our linked list */
    if (req->message) {
	if (!requests) {
	    head = &(pwd_requests);
	    requests = (struct request*)head;
	} else
	    head = &requests->node;
	insert(&req->node, head);
    } else {
	/* Clean up allocation if it wasn't a valid systemd ask file */
	if (req->socket_path)
	    free(req->socket_path);
	free(req);
    }
}

/* Scans the target directory for any files prefixed with "ask." */
void scan_ask_directory(const char *dir_path)
{
    struct dirent *entry;
    char full_path[MAX_LINE];

    DIR *dir = opendir(dir_path);
    if (!dir)
	error("Failed to open systemd ask-password directory: %m");

    while ((entry = readdir(dir)) != NULL) {
	if (entry->d_name[0] == '.')
	    continue;
	if (entry->d_name[0] != 'a')
	    continue;
	/* Look specifically for files starting with "ask." */
	if (strncmp(entry->d_name, "ask.", 4) == 0) {
	    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
	    parse_ask_file(full_path);
	}
    }
    closedir(dir);
}

/*
 * Pops the first systemd password request from the pending list.
 * Transfers ownership of the allocated strings for message and socket_path 
 * to the caller. The caller is responsible for freeing them.
 * Returns 1 if a request was popped, 0 if the list is empty.
 */
int coldstart_pop_request(char **message, char **socket_path)
{
    struct request *req;

    if (!requests)
	return 0;

    /* Check if the doubly linked list is empty */
    if (pwd_requests.next == &pwd_requests) {
	requests = NULL;
	return 0;
    }

    /* Get the first entry and extract the strings */
    req = list_entry(pwd_requests.next, struct request, node);

    *message = req->message;
    *socket_path = req->socket_path;
    
    /* 
     * Defensive: clear the pointers from the struct before freeing it 
     * to clarify that ownership has been transferred to the caller.
     */
    req->message = NULL;
    req->socket_path = NULL;

    /* Remove from list and free the request container */
    delete(&req->node);
    free(req);

    return 1;
}

/*
 * listing.h
 *
 * Copyright 2000 Werner Fink, 2000 SuSE GmbH Nuernberg, Germany.
 *
 * This source is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LISTING_H_
#define _LISTING_H_
#ifdef DEBUG
# include <assert.h>
#endif

#ifndef offsetof
# define offsetof(type,memb)	__builtin_offsetof(type,memb)
#endif

typedef struct list_struct {
    struct list_struct * next, * prev;
} __attribute__((may_alias)) list_t;

/*
 * Insert new entry as next member.
 */
static inline __attribute__((always_inline)) void insert (list_t * new, list_t * here)
{
#ifdef DEBUG
    assert(new != NULL && here != NULL);
    assert(here->next != NULL && here->prev != NULL);
#endif
    list_t * prev = here;
    list_t * next = here->next;

    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/*
 * Set head
 */
static inline void initial (list_t *head)
{
    head->prev = head->next = head;
}

/*
 * Remove entries, note that the pointer its self remains.
 */
#define LIST_POISON1	((void *) 0x100)
#define LIST_POISON2	((void *) 0x200)

static inline __attribute__((always_inline)) void delete (list_t * entry)
{
#ifdef DEBUG
    assert(entry != NULL);
    assert(entry->next != LIST_POISON1 && entry->prev != LIST_POISON2);
#endif
    list_t * prev = entry->prev;
    list_t * next = entry->next;

    next->prev = prev;
    prev->next = next;

    entry->next = (list_t *)LIST_POISON1;
    entry->prev = (list_t *)LIST_POISON2;
}

static inline __attribute__((always_inline)) void join(list_t *list, list_t *head)
{
    list_t *first = list->next;

    if (first != list) {
	list_t *last = list->prev;
       	list_t *at = head->next;

       	first->prev = head;
       	head->next = first;

       	last->next = at;
       	at->prev = last;
    }
}

static inline __attribute__((always_inline)) int list_empty(list_t *head)
{
        return head->next == head;
}

#define list_entry(ptr, type, member)   (__extension__ ({	\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	((type *)( (char *)(__mptr) - offsetof(type,member) )); }))
#define list_for_each(pos, head)	\
	for (pos = (head)->next; __builtin_prefetch(pos->next, 0, 3), pos != (head); pos = pos->next)
#define list_for_each_prev(pos, head)	\
	for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)
#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)
#define list_next_entry(pos, member)	\
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_next_entry(pos, member))
#define list_for_each_entry_safe(pos, n, head, member)                  \
	for (pos = list_first_entry(head, typeof(*pos), member),	\
	       n = list_next_entry(pos, member);			\
	     &pos->member != (head);					\
	     pos = n, n = list_next_entry(n, member))
#endif

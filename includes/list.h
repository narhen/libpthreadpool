#ifndef __LIST_H
#define __LIST_H

struct list {
	struct list *next, *prev;
	void *data;
};

static inline void list_init(struct list *l)
{
	l->next = l->prev = l;
}

static inline void list_add(struct list *l, struct list *new)
{
	new->next = l;
	new->prev = l->prev;
	l->prev->next = new;
	l->prev = new;
}

#define list_add_tail(l, new) (list_add((l), (new)))
#define list_add_head(l, new) (list_add(((l)->next), (new)))

static inline void list_remove(struct list *l)
{
	l->prev->next = l->next;
	l->next->prev = l->prev;
}

#define list_for_each(l, ptr) \
	for ((ptr) = (l)->next; (ptr) != (l); (ptr) = (ptr)->next)

#define list_for_each_tsafe(l, ptr, n) \
	for ((ptr) = (l)->next, (n) = (l)->next->next; (ptr) != (l); \
			(ptr) = (n), (n) = (n)->next)

#endif

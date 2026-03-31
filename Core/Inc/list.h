#ifndef INC_LIST_H_
#define INC_LIST_H_

#include <stddef.h>
#include <stdbool.h>

typedef struct list_node {
    struct list_node *next;
    struct list_node *prev;
} list_node_t;

typedef struct {
    list_node_t sentinel;
} list_t;

#define LIST_ENTRY(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

void list_init(list_t *list);
void list_push_back(list_t *list, list_node_t *node);
void list_push_front(list_t *list, list_node_t *node);
void list_insert_before(list_node_t *pos, list_node_t *node);
list_node_t *list_pop_front(list_t *list);
void list_remove(list_node_t *node);
bool list_is_empty(const list_t *list);

#endif /* INC_LIST_H_ */

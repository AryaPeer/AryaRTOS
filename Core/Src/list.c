#include "list.h"

void list_init(list_t *list)
{
    list->sentinel.next = &list->sentinel;
    list->sentinel.prev = &list->sentinel;
}

void list_push_back(list_t *list, list_node_t *node)
{
    node->prev = list->sentinel.prev;
    node->next = &list->sentinel;
    list->sentinel.prev->next = node;
    list->sentinel.prev = node;
}

void list_push_front(list_t *list, list_node_t *node)
{
    node->next = list->sentinel.next;
    node->prev = &list->sentinel;
    list->sentinel.next->prev = node;
    list->sentinel.next = node;
}

void list_insert_before(list_node_t *pos, list_node_t *node)
{
    node->next = pos;
    node->prev = pos->prev;
    pos->prev->next = node;
    pos->prev = node;
}

list_node_t *list_pop_front(list_t *list)
{
    if (list_is_empty(list)) {
        return NULL;
    }
    list_node_t *node = list->sentinel.next;
    list_remove(node);
    return node;
}

void list_remove(list_node_t *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

bool list_is_empty(const list_t *list)
{
    return list->sentinel.next == &list->sentinel;
}

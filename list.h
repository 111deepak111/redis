#pragma once
#include <stddef.h>

struct DList{
    DList *next=nullptr;
    DList *prev=nullptr;
};

inline void dlist_init(DList *node){
    node->prev=node->next=node;
}

inline bool dlist_empty(DList *node){
    return node->next==node;
}

inline void dlist_detach(DList *node){
    DList *next=node->next;
    DList *prev=node->prev;
    prev->next=next;
    next->prev=prev;
}

inline void dlist_insert_before(DList *target,DList *rookie){
    DList *prev=target->prev;
    prev->next=rookie;
    rookie->prev=prev;
    rookie->next=target;
    target->prev=rookie;
}
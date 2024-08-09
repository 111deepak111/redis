#pragma once
#include <stddef.h>
#include <stdint.h>

struct HNode
{
    HNode *next = nullptr;
    uint64_t hcode = 0;
};

struct HTab
{
    HNode **tab = nullptr; //array of pointers or linked list
    size_t mask = 0;
    size_t size = 0;
};

//has two hashtables in case one needs resizing
struct HMap
{
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode*, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);

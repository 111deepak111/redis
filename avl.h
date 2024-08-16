#pragma once
#include <stdint.h>
#include <stddef.h>

struct AVLNode
{
    uint32_t depth = 0;
    uint32_t cnt = 0;
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    AVLNode *parent = nullptr;
};

inline void avl_init(AVLNode *node)
{
    node->depth = 1;
    node->cnt = 1;
    node->left = node->right = node->parent = nullptr;
}

static AVLNode *avl_fix(AVLNode *node);
static AVLNode *avl_del(AVLNode *node);
static AVLNode *avl_offset(AVLNode *node, int64_t offset);
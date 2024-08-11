#include <stddef.h>
#include <stdint.h>

struct AVLNode{
    AVLNode * parent=nullptr;
    AVLNode *left=nullptr;
    AVLNode *right=nullptr;
    uint32_t depth=0;
    uint32_t cnt=0;
};

static void avl_init(AVLNode *node){
    node->depth=1;
    node->cnt=1;
    node->right=node->left=node->parent=nullptr;
}

static uint32_t avl_depth(AVLNode *node){
    return node?node->depth:0;
}

static uint32_t avl_cnt(AVLNode *node){
    return node?node->cnt:0;
}

static uint32_t max(uint32_t lhs,uint32_t rhs){
    return lhs<rhs?rhs:lhs;
}

static void avl_update(AVLNode *node){
    node->depth=1+max(avl_depth(node->left),avl_depth(node->right));
    node->cnt = 1 + avl_cnt(node->left)+avl_cnt(node->right);
}
 
//          p{parent tree}      p                                     p                    p
//           \                  \                                     \                     \
//           a                  a        c                  a          c                     c      
//          / \                /   +    / \                / \          \                   / \       
//   {left}b   c   ------>>   b        d   e   ------->>  b   d    +     e  ------->>      a   e
//            / \                         /                             /                 / \  /        
//           d   e                       f                             f                 b  d f
//              /
//             f

static AVLNode *rot_left(AVLNode *node){
    AVLNode *newNode=node->right;
    if(newNode->left){
        newNode->left->parent=node;
    }
    node->right=newNode->left;
    newNode->left=node;
    newNode->parent=node->parent;
    node->parent=newNode;
    avl_update(node);
    avl_update(newNode);
    return newNode;
}

//             p{parent tree}      p                           p                                p  
//            /                   /                           /                                /
//           a                  a         c                  a          c                     c      
//          / \                  \  +    / \                / \          \                   / \       
//         c   b   ------>>       b     e   d   ------->>  d   b    +     e  ------->>      a   e
//        / \                          /                                   \               / \   \        
//       e   d                        f                                     f             d   b   f         
//      /
//     f

static AVLNode *rot_right(AVLNode *node){
    AVLNode* newNode=node->left;
    if(newNode->right){
        newNode->right->parent=node;
    }
    node->left=newNode->right;
    newNode->right=node;
    newNode->parent=node->parent;
    node->parent=newNode;
    avl_update(node);
    avl_update(newNode);
    return newNode;
}

static AVLNode *avl_fix_left(AVLNode *node){
    if(avl_depth(node->left->left)<avl_depth(node->left->right))
        node->left=rot_left(node->left);
    return rot_right(node);
}

static AVLNode *avl_fix_right(AVLNode *node){
    if(avl_depth(node->right->right)<avl_depth(node->right->left))
        node->right=rot_right(node->right);
    return rot_left(node);
}

static AVLNode* avl_fix(AVLNode *node){
    while(true){
        avl_update(node);
        uint32_t l=avl_depth(node->left);
        uint32_t r=avl_depth(node->right);
        AVLNode **from = nullptr;
        if(node->parent){
            from=(node->parent->left==node)?&node->parent->left:&node->parent->right;
        }
        if(l==r+2){
            node=avl_fix_left(node);
        }else if(l+2==r){
            node=avl_fix_right(node);
        }
        if(!from) return node;
        *from =node;
        node=node->parent;
    }
}

static AVLNode *avl_del(AVLNode *node){
    if(node->right==nullptr){
        AVLNode *parent=node->parent;
        if(node->left){
            node->left->parent=parent;
        }
        if(parent){
            (parent->left==node?parent->left:parent->right)=node->left;
            return avl_fix(parent);
        }else{
            return node->left;
        }
    }else{
        AVLNode *victim=node->right;
        while(victim->left){
            victim=victim->left;
        }
        AVLNode *root=avl_del(victim);
        *victim=*node;
        if(victim->left){
            victim->left->parent=victim;
        }
        if(victim->right){
            victim->right->parent=victim;
        }
        AVLNode *parent=node->parent;
        if(parent){
            (parent->left==node?parent->left:parent->right)=victim;
            return root;
        }else{
            return victim;
        }
    }
}
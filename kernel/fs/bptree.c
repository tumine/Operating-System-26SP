#include "types.h"
#include "defs.h"
#include "bptree.h"

struct bptree_node {
    int leaf;
    int nkey;
    char *keys[BPTREE_MAX_KEYS + 1];
    struct bptree_node *child[BPTREE_MAX_KEYS + 2];
    struct bptree_values values[BPTREE_MAX_KEYS + 1];
    struct bptree_node *next;
};

void bptree_init(struct bptree *tree, bptree_alloc_fn alloc, void *arg) {
    if (tree == 0) {
        return;
    }
    tree->root = 0;
    tree->alloc = alloc;
    tree->alloc_arg = arg;
    tree->oom = 0;
}

void bptree_reset(struct bptree *tree) {
    if (tree == 0) {
        return;
    }
    tree->root = 0;
    tree->oom = 0;
}

static void *bptree_alloc(struct bptree *tree, uint n) {
    if (tree == 0 || tree->alloc == 0) {
        return 0;
    }
    void *p = tree->alloc(n, tree->alloc_arg);
    if (p == 0) {
        tree->oom = 1;
    }
    return p;
}

static int bptree_keycmp_token(const char *key, const char *token, int len) {
    int i = 0;
    while (key[i] != 0 && i < len) {
        uchar kc = (uchar)key[i];
        uchar tc = (uchar)token[i];
        if (kc != tc) {
            return (int)kc - (int)tc;
        }
        i++;
    }
    if (key[i] == 0 && i == len) {
        return 0;
    }
    return key[i] == 0 ? -1 : 1;
}

static char *bptree_strdup_key(struct bptree *tree, const char *key, int len) {
    char *s = (char *)bptree_alloc(tree, (uint)(len + 1));
    if (s == 0) {
        return 0;
    }
    memmove(s, key, (uint)len);
    s[len] = 0;
    return s;
}

int bptree_values_contains(struct bptree_values *values, void *value) {
    if (values == 0 || value == 0) {
        return 0;
    }
    for (struct bptree_value_chunk *c = values->head; c; c = c->next) {
        for (int i = 0; i < c->n; i++) {
            if (c->values[i] == value) {
                return 1;
            }
        }
    }
    return 0;
}

static int bptree_values_add(struct bptree *tree, struct bptree_values *values, void *value) {
    if (values == 0 || value == 0) {
        return -1;
    }
    if (bptree_values_contains(values, value)) {
        return 0;
    }
    if (values->tail == 0 || values->tail->n >= BPTREE_VALUES_PER_CHUNK) {
        struct bptree_value_chunk *chunk =
            (struct bptree_value_chunk *)bptree_alloc(tree, sizeof(*chunk));
        if (chunk == 0) {
            return -1;
        }
        if (values->tail) {
            values->tail->next = chunk;
        } else {
            values->head = chunk;
        }
        values->tail = chunk;
    }
    values->tail->values[values->tail->n++] = value;
    values->count++;
    return 0;
}

void bptree_values_remove(struct bptree_values *values, void *value) {
    if (values == 0 || value == 0) {
        return;
    }
    for (struct bptree_value_chunk *c = values->head; c; c = c->next) {
        for (int i = 0; i < c->n; i++) {
            if (c->values[i] == value) {
                for (int j = i + 1; j < c->n; j++) {
                    c->values[j - 1] = c->values[j];
                }
                c->n--;
                values->count--;
                return;
            }
        }
    }
}

static struct bptree_node *bptree_new_node(struct bptree *tree, int leaf) {
    struct bptree_node *node = (struct bptree_node *)bptree_alloc(tree, sizeof(*node));
    if (node == 0) {
        return 0;
    }
    node->leaf = leaf;
    return node;
}

struct bptree_values *bptree_lookup(struct bptree *tree, const char *key, int len) {
    if (tree == 0 || key == 0 || len <= 0) {
        return 0;
    }

    struct bptree_node *node = tree->root;
    if (node == 0) {
        // 空树，一定找不到结果
        return 0;
    }

    while (!node->leaf) {
        // 只要当前节点不是叶子节点，就继续下探
        int i = 0;
        // 对当前节点现有的所有 key 从小到大逐个进行比较，找到目标 key 处在哪个子节点
        while (i < node->nkey && bptree_keycmp_token(node->keys[i], key, len) <= 0) {
            i++;
        }
        node = node->child[i];  // 下探到子节点继续寻找
    }

    // 到达叶子节点，逐个 key 比对
    for (int i = 0; i < node->nkey; i++) {
        if (bptree_keycmp_token(node->keys[i], key, len) == 0) {
            // 找到 key，返回 value 映射关系
            return &node->values[i];
        }
    }
    // 未找到 key
    return 0;
}

static int bptree_split_leaf(struct bptree *tree, struct bptree_node *node,
                             char **promoted, struct bptree_node **right) {
    // 在 B+ 树上分配新叶子节点
    struct bptree_node *new_node = bptree_new_node(tree, 1);
    if (new_node == 0) {
        return -1;
    }

    int split = node->nkey / 2;         // 当前叶子节点中分裂点索引号，split 及以后的 key-value 对移动到分裂后的右节点
    int rcount = node->nkey - split;    // 右节点中的 key-value 对数量
    // 逐个迁移右节点中的 key-value 映射关系
    for (int i = 0; i < rcount; i++) {
        new_node->keys[i] = node->keys[split + i];
        new_node->values[i] = node->values[split + i];
    }
    new_node->nkey = rcount;            // 右节点中的 key-value 对数量
    node->nkey = split;                 // 原节点（左节点）的 key-value 对数量

    // 更新叶子节点的链表序
    new_node->next = node->next;
    node->next = new_node;

    // 将右节点中的首 key 作为父节点中的分隔指标，提升给父节点
    int p_len = 0;
    while (new_node->keys[0][p_len]) {
        p_len++;
    }
    char *p_key = bptree_strdup_key(tree, new_node->keys[0], p_len);    // 复制该 key 用于父节点
    if (p_key == 0) {
        return -1;
    }

    *promoted = p_key;  // 复制到父节点的 key
    *right = new_node;  // 分裂出的右节点
    return 0;
}

static int bptree_split_internal(struct bptree *tree, struct bptree_node *node,
                                 char **promoted, struct bptree_node **right) {
    // 在 B+ 树上分配新内部节点
    struct bptree_node *new_node = bptree_new_node(tree, 0);
    if (new_node == 0) {
        return -1;
    }

    int split = node->nkey / 2;             // 内部节点中的分裂点索引号，split 及以后的 key-child 对移动到分裂后的右节点
    int rcount = node->nkey - split - 1;    // 右节点中的 key-child 对数量
    // 逐个迁移右节点中的 key-child 映射关系
    for (int i = 0; i < rcount; i++) {
        new_node->keys[i] = node->keys[split + 1 + i];
        new_node->child[i] = node->child[split + 1 + i];
    }
    new_node->child[rcount] = node->child[node->nkey];  // 将原节点最右侧的子节点指针迁移到右节点中
    new_node->nkey = rcount;

    char *p_key = node->keys[split];    // 取原节点分裂点处的 key 用于父节点
    node->nkey = split;

    *promoted = p_key;
    *right = new_node;
    return 0;
}

static int bptree_insert_rec(struct bptree *tree, struct bptree_node *node,
                             const char *key, int len, void *value,
                             char **promoted, struct bptree_node **right) {
    *promoted = 0;
    *right = 0;

    // 当前节点是叶子节点
    if (node->leaf) {
        // 对当前节点现有的所有 key 从小到大逐个进行比较，找到新 key 应该插入的位置
        int idx = 0;
        while (idx < node->nkey && bptree_keycmp_token(node->keys[idx], key, len) < 0) {
            // 当前索引上的 key < token，继续向下寻找直到找到第一个满足 key >= token 的索引号
            idx++;
        }

        // 检查 key 是否已经存在
        if (idx < node->nkey && bptree_keycmp_token(node->keys[idx], key, len) == 0) {
            // 若当前的 key 已经存在，只需要添加一条已有 key 到新 value 的映射关系即可
            bptree_values_add(tree, &node->values[idx], value);
            return 0;
        }

        // 把 idx 以后的所有 key-value 对向后迁移
        for (int i = node->nkey; i > idx; i--) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1];
        }

        // 把新 key 插入当前节点
        node->keys[idx] = bptree_strdup_key(tree, key, len);
        if (node->keys[idx] == 0) {
            // 新节点的空间分配失败
            return -1;
        }
        // 初始化当前 key 的 value 映射关系
        node->values[idx].count = 0;
        node->values[idx].head = 0;
        node->values[idx].tail = 0;
        bptree_values_add(tree, &node->values[idx], value); // 设置 key-value 映射关系
        node->nkey++;                                       // 更新当前节点的 key 数量

        // 当前节点的 key 数量过多，需要进行节点分裂
        if (node->nkey > BPTREE_MAX_KEYS) {
            return bptree_split_leaf(tree, node, promoted, right);
        }
        return 0;
    }
    // 当前节点是内部节点
    else {
        // 对当前节点现有的所有 key 从小到大逐个进行比较，找到新 key 应该递归插入的位置
        int idx = 0;
        while (idx < node->nkey && bptree_keycmp_token(node->keys[idx], key, len) <= 0) {
            idx++;
        }

        char *child_promoted = 0;               // 子节点分裂输出，指向用于插入父节点的 key 的指针
        struct bptree_node *child_right = 0;    // 子节点分裂输出，右节点指针
        int ret = bptree_insert_rec(tree, node->child[idx], key, len, value,
                                    &child_promoted, &child_right);

        // 子节点发生分裂，需要将提升上来的 key 插入当前节点
        if (child_promoted) {
            // 把 idx 以后的所有 key-value 对向后迁移
            for (int i = node->nkey; i > idx; i--) {
                node->keys[i] = node->keys[i - 1];
                node->child[i + 1] = node->child[i];
            }
            node->keys[idx] = child_promoted;   // 新插入的 key 的名称
            node->child[idx + 1] = child_right; // 维护指向子节点分裂产生的右节点的指针
            node->nkey++;                       // 更新当前节点的 key 数量

            // 当前节点的 key 数量过多，需要进行节点分裂
            if (node->nkey > BPTREE_MAX_KEYS) {
                return bptree_split_internal(tree, node, promoted, right);
            }
        }
        return ret;
    }
}

int bptree_insert(struct bptree *tree, const char *key, int len, void *value) {
    if (tree == 0 || len <= 0 || value == 0) {
        return -1;
    }

    // 树为空，则创建一个叶子节点作为根节点
    if (tree->root == 0) {
        tree->root = bptree_new_node(tree, 1);
        if (tree->root == 0) {
            return -1;
        }
    }

    char *promoted = 0;             // 根节点分裂输出，指向用于插入新根节点的 key 的指针
    struct bptree_node *right = 0;  // 根节点分裂输出，右节点指针
    // 从根节点开始向下递归调用 bptree_insert_rec 尝试插入传入的 key-value 对
    int ret = bptree_insert_rec(tree, tree->root, key, len, value, &promoted, &right);

    // 根节点需要进行分裂
    if (promoted) {
        struct bptree_node *new_root = bptree_new_node(tree, 0);    // 创建一个新节点作为新的根节点
        if (new_root == 0) {
            return -1;
        }
        new_root->keys[0] = promoted;
        new_root->child[0] = tree->root;
        new_root->child[1] = right;
        new_root->nkey = 1;
        tree->root = new_root;              // 更新整棵树的根节点指针
    }

    return ret;
}

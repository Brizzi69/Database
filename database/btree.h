#ifndef BTREE_H
#define BTREE_H

#include <stdbool.h>
#include <stdint.h>

#include "database.h"
#include "pager.h"

// All of this is #define, not "const uint32_t", so that every file
// including this header (and database.h/pager.h, for ROW_SIZE/PAGE_SIZE)
// resolves the same compile-time values -- a const initialized from
// another translation unit's const isn't a constant expression in C.

// Common node header (shared by leaf and internal nodes).
#define NODE_TYPE_SIZE (sizeof(uint8_t))
#define NODE_TYPE_OFFSET 0
#define IS_ROOT_SIZE (sizeof(uint8_t))
#define IS_ROOT_OFFSET (NODE_TYPE_OFFSET + NODE_TYPE_SIZE)
#define PARENT_POINTER_SIZE (sizeof(uint32_t))
#define PARENT_POINTER_OFFSET (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE (NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

// Leaf node header/cell layout.
#define LEAF_NODE_NUM_CELLS_SIZE (sizeof(uint32_t))
#define LEAF_NODE_NUM_CELLS_OFFSET (COMMON_NODE_HEADER_SIZE)
#define LEAF_NODE_NEXT_LEAF_SIZE (sizeof(uint32_t))
#define LEAF_NODE_NEXT_LEAF_OFFSET (LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE)
#define LEAF_NODE_HEADER_SIZE (COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE)

#define LEAF_NODE_KEY_SIZE (sizeof(uint32_t))
#define LEAF_NODE_KEY_OFFSET 0
#define LEAF_NODE_VALUE_SIZE (ROW_SIZE)
#define LEAF_NODE_VALUE_OFFSET (LEAF_NODE_KEY_SIZE)
#define LEAF_NODE_CELL_SIZE (LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)

#define LEAF_NODE_RIGHT_SPLIT_COUNT ((LEAF_NODE_MAX_CELLS + 1) / 2)
#define LEAF_NODE_LEFT_SPLIT_COUNT ((LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT)

// Internal node header/cell layout.
#define INTERNAL_NODE_NUM_KEYS_SIZE (sizeof(uint32_t))
#define INTERNAL_NODE_NUM_KEYS_OFFSET (COMMON_NODE_HEADER_SIZE)
#define INTERNAL_NODE_RIGHT_CHILD_SIZE (sizeof(uint32_t))
#define INTERNAL_NODE_RIGHT_CHILD_OFFSET (INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE)
#define INTERNAL_NODE_HEADER_SIZE (COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE)

#define INTERNAL_NODE_KEY_SIZE (sizeof(uint32_t))
#define INTERNAL_NODE_CHILD_SIZE (sizeof(uint32_t))
#define INTERNAL_NODE_CELL_SIZE (INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE)
#define INTERNAL_NODE_SPACE_FOR_CELLS (PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE)
#define INTERNAL_NODE_MAX_CELLS (INTERNAL_NODE_SPACE_FOR_CELLS / INTERNAL_NODE_CELL_SIZE)

enum NodeType { NODE_LEAF, NODE_INTERNAL };

uint8_t *node_type(void *node);
bool is_node_root(void *node);
void set_node_root(void *node, bool is_root);
uint32_t *node_parent(void *node);

uint32_t *leaf_node_num_cells(void *node);
uint32_t *leaf_node_next_leaf(void *node);
void *leaf_node_cell(void *node, uint32_t cell_num);
uint32_t *leaf_node_key(void *node, uint32_t cell_num);
void *leaf_node_value(void *node, uint32_t cell_num);
void initialize_leaf_node(void *node);

uint32_t *internal_node_num_keys(void *node);
uint32_t *internal_node_right_child(void *node);
void *internal_node_cell(void *node, uint32_t cell_num);
uint32_t *internal_node_child(void *node, uint32_t child_num);
uint32_t *internal_node_key(void *node, uint32_t key_num);
void initialize_internal_node(void *node);

uint32_t leaf_node_find(Table *table, uint32_t page_num, uint32_t key);
uint32_t internal_node_find(Table *table, uint32_t page_num, uint32_t key);
uint32_t get_node_max_key(void *node);

// Finds the leaf page a key belongs in (or would be inserted into).
uint32_t table_find_leaf(Table *table, uint32_t key);

void internal_node_insert(Table *table, uint32_t parent_page_num, uint32_t child_page_num);
void create_new_root(Table *table, uint32_t right_child_page_num);
void leaf_node_split_and_insert(Table *table, uint32_t page_num, uint32_t key, Row *value);

// Debug helper backing the REPL's ".btree" meta-command.
void print_tree(Pager *pager, uint32_t page_num, uint32_t indentation_level);

#endif

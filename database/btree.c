#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btree.h"

// Node accessor functions
uint8_t *node_type(void *node) { return node + NODE_TYPE_OFFSET; }

bool is_node_root(void *node) {
    uint8_t value = *(uint8_t *)(node + IS_ROOT_OFFSET);
    return (bool)value;
}

void set_node_root(void *node, bool is_root) {
    uint8_t value = is_root;
    *(uint8_t *)(node + IS_ROOT_OFFSET) = value;
}

uint32_t *node_parent(void *node) { return node + PARENT_POINTER_OFFSET; }

uint32_t *leaf_node_num_cells(void *node) {
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

uint32_t *leaf_node_next_leaf(void *node) {
    return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}

void *leaf_node_cell(void *node, uint32_t cell_num) {
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t *leaf_node_key(void *node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_OFFSET;
}

void *leaf_node_value(void *node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num) + LEAF_NODE_VALUE_OFFSET;
}

void initialize_leaf_node(void *node) {
    *node_type(node) = NODE_LEAF;
    *leaf_node_num_cells(node) = 0;
    *leaf_node_next_leaf(node) = 0;
}

uint32_t *internal_node_num_keys(void *node) {
    return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t *internal_node_right_child(void *node) {
    return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

void *internal_node_cell(void *node, uint32_t cell_num) {
    return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t *internal_node_child(void *node, uint32_t child_num) {
    uint32_t num_keys = *internal_node_num_keys(node);
    if (child_num > num_keys) {
        printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
        exit(EXIT_FAILURE);
    } else if (child_num == num_keys) {
        return internal_node_right_child(node);
    } else {
        return internal_node_cell(node, child_num);
    }
}

uint32_t *internal_node_key(void *node, uint32_t key_num) {
    return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

void initialize_internal_node(void *node) {
    *node_type(node) = NODE_INTERNAL;
    *internal_node_num_keys(node) = 0;
}

// B-tree search
uint32_t leaf_node_find(Table *table, uint32_t page_num, uint32_t key) {
    void *node = get_page(table->pager, page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;

    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);

        if (key == key_at_index) {
            return index;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }
    return min_index;
}

uint32_t internal_node_find(Table *table, uint32_t page_num, uint32_t key) {
    void *node = get_page(table->pager, page_num);
    uint32_t num_keys = *internal_node_num_keys(node);

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_keys;

    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_to_right = *internal_node_key(node, index);

        if (key_to_right >= key) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }
    return min_index;
}

uint32_t get_node_max_key(void *node) {
    if (*node_type(node) == NODE_LEAF) {
        return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
    }
    return *internal_node_key(node, *internal_node_num_keys(node) - 1);
}

uint32_t table_find_leaf(Table *table, uint32_t key) {
    uint32_t page_num = table->root_page_num;
    void *node = get_page(table->pager, page_num);

    while (*node_type(node) != NODE_LEAF) {
        uint32_t child_index = internal_node_find(table, page_num, key);
        page_num = *internal_node_child(node, child_index);
        node = get_page(table->pager, page_num);
    }

    return page_num;
}

static void update_internal_node_key(Table *table, uint32_t node_page_num, uint32_t old_key, uint32_t new_key) {
    void *node = get_page(table->pager, node_page_num);
    uint32_t old_child_index = internal_node_find(table, node_page_num, old_key);
    *internal_node_key(node, old_child_index) = new_key;
}

// Inserts a newly-created child page into a parent internal node. The
// parent is assumed to have room; if it's already full this is a tree
// depth beyond what this implementation supports.
void internal_node_insert(Table *table, uint32_t parent_page_num, uint32_t child_page_num) {
    void *parent = get_page(table->pager, parent_page_num);
    void *child = get_page(table->pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(child);
    uint32_t index = internal_node_find(table, parent_page_num, child_max_key);

    uint32_t original_num_keys = *internal_node_num_keys(parent);

    if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
        printf("Error: internal node is full. Splitting internal nodes is not yet supported.\n");
        exit(EXIT_FAILURE);
    }

    uint32_t right_child_page_num = *internal_node_right_child(parent);
    void *right_child = get_page(table->pager, right_child_page_num);

    if (child_max_key > get_node_max_key(right_child)) {
        // Write directly to the raw cell rather than through internal_node_child():
        // that accessor redirects child_num == num_keys to the right_child field,
        // which is exactly the (not yet bumped) index used here.
        *(uint32_t *)internal_node_cell(parent, original_num_keys) = right_child_page_num;
        *internal_node_key(parent, original_num_keys) = get_node_max_key(right_child);
        *internal_node_right_child(parent) = child_page_num;
    } else {
        for (uint32_t i = original_num_keys; i > index; i--) {
            void *destination = internal_node_cell(parent, i);
            void *source = internal_node_cell(parent, i - 1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        *internal_node_child(parent, index) = child_page_num;
        *internal_node_key(parent, index) = child_max_key;
    }

    *internal_node_num_keys(parent) = original_num_keys + 1;
    *node_parent(child) = parent_page_num;
}

void create_new_root(Table *table, uint32_t right_child_page_num) {
    void *root = get_page(table->pager, table->root_page_num);
    void *right_child = get_page(table->pager, right_child_page_num);
    uint32_t left_child_page_num = allocate_page(table->pager);
    void *left_child = get_page(table->pager, left_child_page_num);

    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    initialize_internal_node(root);
    set_node_root(root, true);
    *internal_node_num_keys(root) = 1;
    *internal_node_child(root, 0) = left_child_page_num;
    uint32_t left_child_max_key = get_node_max_key(left_child);
    *internal_node_key(root, 0) = left_child_max_key;
    *internal_node_right_child(root) = right_child_page_num;
    *node_parent(left_child) = table->root_page_num;
    *node_parent(right_child) = table->root_page_num;
}

void leaf_node_split_and_insert(Table *table, uint32_t page_num, uint32_t key, Row *value) {
    void *old_node = get_page(table->pager, page_num);
    uint32_t old_num_cells = *leaf_node_num_cells(old_node);
    uint32_t old_max = *leaf_node_key(old_node, old_num_cells - 1);
    uint32_t split_boundary_key = *leaf_node_key(old_node, LEAF_NODE_LEFT_SPLIT_COUNT - 1);

    uint32_t new_page_num = allocate_page(table->pager);
    void *new_node = get_page(table->pager, new_page_num);
    initialize_leaf_node(new_node);
    set_node_root(new_node, false);
    *node_parent(new_node) = *node_parent(old_node);
    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
    *leaf_node_next_leaf(old_node) = new_page_num;

    uint32_t num_moved = old_num_cells - LEAF_NODE_LEFT_SPLIT_COUNT;
    for (uint32_t i = 0; i < num_moved; i++) {
        void *destination = leaf_node_cell(new_node, i);
        void *source = leaf_node_cell(old_node, i + LEAF_NODE_LEFT_SPLIT_COUNT);
        memcpy(destination, source, LEAF_NODE_CELL_SIZE);
    }

    *leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *leaf_node_num_cells(new_node) = num_moved;

    uint32_t index_to_insert;
    void *node_to_insert;
    uint32_t page_to_insert;

    if (key <= split_boundary_key) {
        node_to_insert = old_node;
        page_to_insert = page_num;
    } else {
        node_to_insert = new_node;
        page_to_insert = new_page_num;
    }

    uint32_t num_cells = *leaf_node_num_cells(node_to_insert);
    index_to_insert = leaf_node_find(table, page_to_insert, key);

    for (uint32_t i = num_cells; i > index_to_insert; i--) {
        void *destination = leaf_node_cell(node_to_insert, i);
        void *source = leaf_node_cell(node_to_insert, i - 1);
        memcpy(destination, source, LEAF_NODE_CELL_SIZE);
    }

    *leaf_node_key(node_to_insert, index_to_insert) = key;
    serialize_row(value, leaf_node_value(node_to_insert, index_to_insert));
    *leaf_node_num_cells(node_to_insert) = num_cells + 1;

    if (is_node_root(old_node)) {
        create_new_root(table, new_page_num);
    } else {
        uint32_t parent_page_num = *node_parent(old_node);
        update_internal_node_key(table, parent_page_num, old_max, split_boundary_key);
        internal_node_insert(table, parent_page_num, new_page_num);
    }
}

static void print_indented(uint32_t level) {
    for (uint32_t i = 0; i < level; i++) printf("  ");
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t indentation_level) {
    void *node = get_page(pager, page_num);
    uint32_t num_keys;

    if (*node_type(node) == NODE_LEAF) {
        num_keys = *leaf_node_num_cells(node);
        print_indented(indentation_level);
        printf("- leaf (size %d)\n", num_keys);
        for (uint32_t i = 0; i < num_keys; i++) {
            print_indented(indentation_level + 1);
            printf("- %d\n", *leaf_node_key(node, i));
        }
    } else {
        num_keys = *internal_node_num_keys(node);
        print_indented(indentation_level);
        printf("- internal (size %d)\n", num_keys);
        for (uint32_t i = 0; i < num_keys; i++) {
            uint32_t child = *internal_node_child(node, i);
            print_tree(pager, child, indentation_level + 1);
            print_indented(indentation_level + 1);
            printf("- key %d\n", *internal_node_key(node, i));
        }
        uint32_t right_child = *internal_node_right_child(node);
        print_tree(pager, right_child, indentation_level + 1);
    }
}

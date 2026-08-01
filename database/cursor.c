#include <stdlib.h>

#include "cursor.h"
#include "btree.h"
#include "pager.h"

Cursor *table_start(Table *table) {
    uint32_t page_num = table->root_page_num;
    void *node = get_page(table->pager, page_num);

    while (*node_type(node) != NODE_LEAF) {
        uint32_t child_page_num = *internal_node_child(node, 0);
        page_num = child_page_num;
        node = get_page(table->pager, page_num);
    }

    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;
    cursor->cell_num = 0;
    uint32_t num_cells = *leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);
    return cursor;
}

Cursor *table_end(Table *table) {
    uint32_t page_num = table->root_page_num;
    void *node = get_page(table->pager, page_num);

    while (*node_type(node) != NODE_LEAF) {
        uint32_t child_page_num = *internal_node_right_child(node);
        page_num = child_page_num;
        node = get_page(table->pager, page_num);
    }

    uint32_t num_cells = *leaf_node_num_cells(node);
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;
    cursor->cell_num = num_cells;
    cursor->end_of_table = true;
    return cursor;
}

void cursor_advance(Cursor *cursor) {
    void *node = get_page(cursor->table->pager, cursor->page_num);

    cursor->cell_num += 1;
    if (cursor->cell_num >= *leaf_node_num_cells(node)) {
        uint32_t next_leaf = *leaf_node_next_leaf(node);
        if (next_leaf == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_leaf;
            cursor->cell_num = 0;
        }
    }
}

void *cursor_value(Cursor *cursor) {
    void *page = get_page(cursor->table->pager, cursor->page_num);
    return leaf_node_value(page, cursor->cell_num);
}

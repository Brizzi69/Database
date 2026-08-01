#ifndef CURSOR_H
#define CURSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "database.h"

// An abstraction for traversing a table's B-tree (via leaf sibling
// pointers) without the caller needing to know the underlying page layout.
typedef struct {
    Table *table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} Cursor;

Cursor *table_start(Table *table);
Cursor *table_end(Table *table);
void cursor_advance(Cursor *cursor);
void *cursor_value(Cursor *cursor);

#endif

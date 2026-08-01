#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor.h"
#include "cursor.h"
#include "btree.h"

static bool row_matches_where(const Row *row, const WhereClause *where) {
    if (!where->has_where) return true;
    switch (where->column) {
        case COLUMN_ID: return row->id == where->id_value;
        case COLUMN_USERNAME: return strcmp(row->username, where->value) == 0;
        case COLUMN_EMAIL: return strcmp(row->email, where->value) == 0;
    }
    return false;
}

void executor_select(const Statement *statement, Table *table, DbResult *result) {
    Cursor *cursor = table_start(table);

    uint32_t capacity = 16;
    Row *rows = malloc(capacity * sizeof(Row));
    if (!rows) {
        free(cursor);
        result->success = false;
        result->error_code = DB_ERR_INTERNAL;
        snprintf(result->error_message, sizeof(result->error_message), "out of memory");
        return;
    }

    uint32_t count = 0;
    Row row;
    while (!(cursor->end_of_table)) {
        deserialize_row(cursor_value(cursor), &row);

        if (row_matches_where(&row, &statement->where)) {
            if (count >= capacity) {
                capacity *= 2;
                Row *grown = realloc(rows, capacity * sizeof(Row));
                if (!grown) {
                    free(rows);
                    free(cursor);
                    result->success = false;
                    result->error_code = DB_ERR_INTERNAL;
                    snprintf(result->error_message, sizeof(result->error_message), "out of memory");
                    return;
                }
                rows = grown;
            }
            rows[count++] = row;
        }

        cursor_advance(cursor);
    }

    free(cursor);

    result->success = true;
    result->rows = rows;
    result->row_count = count;
}

void executor_insert(const Statement *statement, Table *table, DbResult *result) {
    const Row *row_to_insert = &statement->row_to_insert;
    uint32_t key_to_insert = row_to_insert->id;

    uint32_t leaf_page_num = table_find_leaf(table, key_to_insert);
    void *node = get_page(table->pager, leaf_page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    uint32_t index_to_insert = leaf_node_find(table, leaf_page_num, key_to_insert);

    if (index_to_insert < num_cells) {
        uint32_t key_at_index = *leaf_node_key(node, index_to_insert);
        if (key_at_index == key_to_insert) {
            result->success = false;
            result->error_code = DB_ERR_DUPLICATE_KEY;
            snprintf(result->error_message, sizeof(result->error_message),
                     "duplicate key '%u'", key_to_insert);
            return;
        }
    }

    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        leaf_node_split_and_insert(table, leaf_page_num, key_to_insert, (Row *)row_to_insert);
        result->success = true;
        result->rows_affected = 1;
        return;
    }

    for (uint32_t i = num_cells; i > index_to_insert; i--) {
        void *destination = leaf_node_cell(node, i);
        void *source = leaf_node_cell(node, i - 1);
        memcpy(destination, source, LEAF_NODE_CELL_SIZE);
    }

    *leaf_node_key(node, index_to_insert) = key_to_insert;
    serialize_row((Row *)row_to_insert, leaf_node_value(node, index_to_insert));
    *leaf_node_num_cells(node) = num_cells + 1;

    result->success = true;
    result->rows_affected = 1;
}

void executor_update(const Statement *statement, Table *table, DbResult *result) {
    uint32_t updated_count = 0;
    Cursor *cursor = table_start(table);
    Row row;

    while (!(cursor->end_of_table)) {
        void *node = get_page(table->pager, cursor->page_num);
        deserialize_row(leaf_node_value(node, cursor->cell_num), &row);

        if (row_matches_where(&row, &statement->where)) {
            if (statement->update_username_set) {
                strcpy(row.username, statement->row_to_update.username);
            }
            if (statement->update_email_set) {
                strcpy(row.email, statement->row_to_update.email);
            }
            serialize_row(&row, leaf_node_value(node, cursor->cell_num));
            updated_count++;
        }

        cursor_advance(cursor);
    }

    free(cursor);

    result->success = true;
    result->rows_affected = updated_count;
}

void executor_delete(const Statement *statement, Table *table, DbResult *result) {
    uint32_t deleted_count = 0;
    Cursor *cursor = table_start(table);
    Row row;

    while (!(cursor->end_of_table)) {
        void *node = get_page(table->pager, cursor->page_num);
        uint32_t num_cells = *leaf_node_num_cells(node);

        if (cursor->cell_num >= num_cells) {
            uint32_t next_leaf = *leaf_node_next_leaf(node);
            if (next_leaf == 0) {
                cursor->end_of_table = true;
                continue;
            }
            cursor->page_num = next_leaf;
            cursor->cell_num = 0;
            continue;
        }

        deserialize_row(leaf_node_value(node, cursor->cell_num), &row);

        if (row_matches_where(&row, &statement->where)) {
            for (uint32_t j = cursor->cell_num; j < num_cells - 1; j++) {
                void *dest = leaf_node_cell(node, j);
                void *src = leaf_node_cell(node, j + 1);
                memcpy(dest, src, LEAF_NODE_CELL_SIZE);
            }
            *leaf_node_num_cells(node) = num_cells - 1;
            deleted_count++;
            // Stay at the same cell_num: the next row just shifted into it.
        } else {
            cursor->cell_num++;
        }
    }

    free(cursor);

    result->success = true;
    result->rows_affected = deleted_count;
}

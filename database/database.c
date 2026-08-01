#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "database.h"
#include "btree.h"
#include "parser.h"
#include "executor.h"

void serialize_row(Row *source, void *destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    strncpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

void deserialize_row(void *source, Row *destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

Database *db_open(const char *filename) {
    Pager *pager = pager_open(filename);

    Database *db = malloc(sizeof(Database));
    db->pager = pager;

    if (pager->num_pages == 0) {
        void *catalog_page = get_page(pager, 0);
        memset(catalog_page, 0, PAGE_SIZE);
        Catalog *catalog = (Catalog *)catalog_page;
        catalog->num_tables = 0;
        pager->num_pages += 1;
    }

    return db;
}

void db_close(Database *db) {
    Pager *pager = db->pager;

    for (uint32_t i = 0; i < pager->num_pages; i++) {
        if (pager->pages[i] == NULL) continue;
        pager_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    int result = close(pager->file_descriptor);
    if (result == -1) {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }
    free(pager);
    free(db);
}

bool db_create_table(Database *db, const char *name) {
    Catalog *catalog = (Catalog *)get_page(db->pager, 0);

    for (uint32_t i = 0; i < catalog->num_tables; i++) {
        if (strcmp(catalog->entries[i].name, name) == 0) {
            return false;
        }
    }
    if (catalog->num_tables >= MAX_TABLES) {
        return false;
    }

    uint32_t root_page_num = allocate_page(db->pager);
    void *root = get_page(db->pager, root_page_num);
    initialize_leaf_node(root); // also sets node type
    set_node_root(root, true);
    *node_parent(root) = 0;

    strncpy(catalog->entries[catalog->num_tables].name, name, TABLE_NAME_SIZE - 1);
    catalog->entries[catalog->num_tables].name[TABLE_NAME_SIZE - 1] = '\0';
    catalog->entries[catalog->num_tables].root_page_num = root_page_num;
    catalog->num_tables += 1;

    return true;
}

bool db_lookup_table(Database *db, const char *name, Table *out) {
    Catalog *catalog = (Catalog *)get_page(db->pager, 0);

    for (uint32_t i = 0; i < catalog->num_tables; i++) {
        if (strcmp(catalog->entries[i].name, name) == 0) {
            out->pager = db->pager;
            out->root_page_num = catalog->entries[i].root_page_num;
            return true;
        }
    }
    return false;
}

uint32_t db_list_tables(Database *db, char out_names[][TABLE_NAME_SIZE], uint32_t max_names) {
    Catalog *catalog = (Catalog *)get_page(db->pager, 0);
    uint32_t count = catalog->num_tables < max_names ? catalog->num_tables : max_names;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(out_names[i], catalog->entries[i].name, TABLE_NAME_SIZE);
    }
    return count;
}

static void set_prepare_error(DbResult *result, PrepareResult prepare_result) {
    result->success = false;
    result->error_code = DB_ERR_SYNTAX;
    switch (prepare_result) {
        case PREPARE_NEGATIVE_ID:
            snprintf(result->error_message, sizeof(result->error_message), "id must be positive");
            break;
        case PREPARE_STRING_TOO_LONG:
            snprintf(result->error_message, sizeof(result->error_message), "value too long");
            break;
        case PREPARE_UNRECOGNIZED_STATEMENT:
            snprintf(result->error_message, sizeof(result->error_message), "unrecognized statement");
            break;
        case PREPARE_SYNTAX_ERROR:
        default:
            snprintf(result->error_message, sizeof(result->error_message), "syntax error");
            break;
    }
}

DbResult db_execute(Database *db, const char *sql) {
    DbResult result;
    memset(&result, 0, sizeof(result));

    while (isspace((unsigned char)*sql)) sql++;

    Statement statement;
    PrepareResult prepare_result = prepare_statement(sql, &statement);
    if (prepare_result != PREPARE_SUCCESS) {
        set_prepare_error(&result, prepare_result);
        return result;
    }

    switch (statement.type) {
        case STATEMENT_SELECT: result.statement_kind = DB_STMT_SELECT; break;
        case STATEMENT_INSERT: result.statement_kind = DB_STMT_INSERT; break;
        case STATEMENT_UPDATE: result.statement_kind = DB_STMT_UPDATE; break;
        case STATEMENT_DELETE: result.statement_kind = DB_STMT_DELETE; break;
        case STATEMENT_CREATE_TABLE: result.statement_kind = DB_STMT_CREATE_TABLE; break;
    }

    if (statement.type == STATEMENT_CREATE_TABLE) {
        if (db_create_table(db, statement.table_name)) {
            result.success = true;
        } else {
            result.success = false;
            result.error_code = DB_ERR_TABLE_EXISTS;
            snprintf(result.error_message, sizeof(result.error_message),
                     "table '%s' already exists or table limit reached", statement.table_name);
        }
        return result;
    }

    Table table;
    if (!db_lookup_table(db, statement.table_name, &table)) {
        result.success = false;
        result.error_code = DB_ERR_NO_SUCH_TABLE;
        snprintf(result.error_message, sizeof(result.error_message),
                 "no such table '%s'", statement.table_name);
        return result;
    }

    switch (statement.type) {
        case STATEMENT_SELECT: executor_select(&statement, &table, &result); break;
        case STATEMENT_INSERT: executor_insert(&statement, &table, &result); break;
        case STATEMENT_UPDATE: executor_update(&statement, &table, &result); break;
        case STATEMENT_DELETE: executor_delete(&statement, &table, &result); break;
        default: break;
    }
    return result;
}

void db_result_free(DbResult *result) {
    free(result->rows);
    result->rows = NULL;
    result->row_count = 0;
}

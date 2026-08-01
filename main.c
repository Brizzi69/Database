#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "database/database.h"
#include "database/btree.h" // for print_tree, backing the ".btree" debug command

typedef struct {
    char *buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

static InputBuffer *new_input_buffer(void) {
    InputBuffer *input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;
    return input_buffer;
}

static void print_prompt(void) { printf("db > "); }

static bool read_input(InputBuffer *input_buffer) {
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read < 0) return false;

    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0;
    return true;
}

static void close_input_buffer(InputBuffer *input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

static void print_row(const Row *row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

static void print_tables(Database *db) {
    char names[MAX_TABLES][TABLE_NAME_SIZE];
    uint32_t count = db_list_tables(db, names, MAX_TABLES);
    if (count == 0) {
        printf("(no tables)\n");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        printf("%s\n", names[i]);
    }
}

typedef enum { META_COMMAND_SUCCESS, META_COMMAND_UNRECOGNIZED_COMMAND } MetaCommandResult;

static MetaCommandResult do_meta_command(InputBuffer *input_buffer, Database *db) {
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        close_input_buffer(input_buffer);
        db_close(db);
        exit(EXIT_SUCCESS);
    } else if (strcmp(input_buffer->buffer, ".tables") == 0) {
        print_tables(db);
        return META_COMMAND_SUCCESS;
    } else if (strncmp(input_buffer->buffer, ".btree", 6) == 0) {
        char *name = input_buffer->buffer + 6;
        while (*name == ' ') name++;
        if (*name == '\0') {
            printf("Usage: .btree <table_name>\n");
            return META_COMMAND_SUCCESS;
        }
        Table table;
        if (!db_lookup_table(db, name, &table)) {
            printf("Error: no such table '%s'.\n", name);
            return META_COMMAND_SUCCESS;
        }
        printf("Tree:\n");
        print_tree(table.pager, table.root_page_num, 0);
        return META_COMMAND_SUCCESS;
    } else {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

static void run_sql(Database *db, const char *sql) {
    DbResult result = db_execute(db, sql);

    if (!result.success) {
        printf("Error: %s\n", result.error_message);
        db_result_free(&result);
        return;
    }

    switch (result.statement_kind) {
        case DB_STMT_SELECT:
            for (uint32_t i = 0; i < result.row_count; i++) {
                print_row(&result.rows[i]);
            }
            printf("%u row(s) returned.\n", result.row_count);
            break;
        case DB_STMT_INSERT:
        case DB_STMT_UPDATE:
        case DB_STMT_DELETE:
            printf("%u row(s) affected.\n", result.rows_affected);
            break;
        case DB_STMT_CREATE_TABLE:
            printf("Table created.\n");
            break;
    }

    db_result_free(&result);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    Database *db = db_open(argv[1]);
    InputBuffer *input_buffer = new_input_buffer();

    while (true) {
        print_prompt();

        if (!read_input(input_buffer)) {
            printf("\n");
            break;
        }

        if (input_buffer->buffer[0] == '\0') {
            continue;
        }

        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, db)) {
                case META_COMMAND_SUCCESS: continue;
                case META_COMMAND_UNRECOGNIZED_COMMAND:
                    printf("Unrecognized command '%s'\n", input_buffer->buffer);
                    continue;
            }
        }

        run_sql(db, input_buffer->buffer);
    }

    close_input_buffer(input_buffer);
    db_close(db);
    exit(EXIT_SUCCESS);
}

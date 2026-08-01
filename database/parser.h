#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "database.h"

typedef enum {
    STATEMENT_CREATE_TABLE,
    STATEMENT_INSERT,
    STATEMENT_SELECT,
    STATEMENT_DELETE,
    STATEMENT_UPDATE
} StatementType;

typedef enum {
    COLUMN_ID,
    COLUMN_USERNAME,
    COLUMN_EMAIL
} ColumnType;

typedef struct {
    bool has_where;
    ColumnType column;
    char value[256];
    uint32_t id_value;
} WhereClause;

typedef struct {
    StatementType type;
    char table_name[TABLE_NAME_SIZE];
    Row row_to_insert;
    WhereClause where;

    // For UPDATE: which columns SET actually mentioned (an update can set
    // just username, just email, or both).
    Row row_to_update;
    bool update_username_set;
    bool update_email_set;
} Statement;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_SYNTAX_ERROR,
    PREPARE_STRING_TOO_LONG,
    PREPARE_NEGATIVE_ID
} PrepareResult;

// Parses one SQL statement: "SELECT * FROM t [WHERE col = val];",
// "INSERT INTO t VALUES (id, 'username', 'email');",
// "UPDATE t SET username = '..' [, email = '..'] WHERE col = val;",
// "DELETE FROM t WHERE col = val;", or "CREATE TABLE t;". Keywords are
// case-insensitive; the trailing ';' is optional.
PrepareResult prepare_statement(const char *sql, Statement *statement);

#endif

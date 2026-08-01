#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "parser.h"

// Case-insensitively matches `keyword` at *cursor. To avoid matching a
// longer identifier with the same prefix (e.g. "selection" for "select"),
// the keyword must be followed by whitespace, '(', or end-of-string.
// Advances *cursor past the keyword and any following whitespace on match.
static bool consume_keyword(const char **cursor, const char *keyword) {
    size_t len = strlen(keyword);
    if (strncasecmp(*cursor, keyword, len) != 0) return false;

    char next = (*cursor)[len];
    if (next != '\0' && !isspace((unsigned char)next) && next != '(') return false;

    *cursor += len;
    while (isspace((unsigned char)**cursor)) (*cursor)++;
    return true;
}

// Reads an identifier (table name) up to the next whitespace, '(', or ';'.
static bool parse_identifier(const char **cursor, char *out, size_t out_size) {
    const char *start = *cursor;
    size_t len = 0;
    while (**cursor && !isspace((unsigned char)**cursor) && **cursor != '(' && **cursor != ';') {
        (*cursor)++;
        len++;
    }
    if (len == 0 || len >= out_size) return false;

    memcpy(out, start, len);
    out[len] = '\0';
    while (isspace((unsigned char)**cursor)) (*cursor)++;
    return true;
}

typedef enum { LITERAL_OK, LITERAL_SYNTAX_ERROR, LITERAL_TOO_LONG } LiteralResult;

// Parses a single-quoted string literal at *cursor into out (bounded).
static LiteralResult parse_string_literal(const char **cursor, char *out, size_t out_size) {
    if (**cursor != '\'') return LITERAL_SYNTAX_ERROR;
    const char *content = *cursor + 1;

    const char *end_quote = strchr(content, '\'');
    if (!end_quote) return LITERAL_SYNTAX_ERROR;

    size_t len = (size_t)(end_quote - content);
    if (len >= out_size) return LITERAL_TOO_LONG;

    memcpy(out, content, len);
    out[len] = '\0';
    *cursor = end_quote + 1;
    while (isspace((unsigned char)**cursor)) (*cursor)++;
    return LITERAL_OK;
}

// Parses an optional "WHERE id = N" / "WHERE username = '..'" /
// "WHERE email = '..'" clause. An empty/semicolon-only remainder means
// there's no WHERE clause at all (not an error).
static bool parse_where_clause(const char *cursor, WhereClause *where) {
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0' || *cursor == ';') {
        where->has_where = false;
        return true;
    }
    if (!consume_keyword(&cursor, "where")) return false;
    where->has_where = true;

    if (consume_keyword(&cursor, "id")) {
        if (*cursor != '=') return false;
        cursor++;
        while (isspace((unsigned char)*cursor)) cursor++;

        char *end;
        long value = strtol(cursor, &end, 10);
        if (end == cursor || value < 0) return false;

        where->column = COLUMN_ID;
        where->id_value = (uint32_t)value;
        snprintf(where->value, sizeof(where->value), "%ld", value);
        return true;
    }

    if (consume_keyword(&cursor, "username")) {
        where->column = COLUMN_USERNAME;
    } else if (consume_keyword(&cursor, "email")) {
        where->column = COLUMN_EMAIL;
    } else {
        return false;
    }

    if (*cursor != '=') return false;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;

    LiteralResult lit = parse_string_literal(&cursor, where->value, sizeof(where->value));
    return lit == LITERAL_OK;
}

static PrepareResult prepare_create_table(const char *sql, Statement *statement) {
    statement->type = STATEMENT_CREATE_TABLE;
    const char *cursor = sql;

    if (!consume_keyword(&cursor, "create")) return PREPARE_SYNTAX_ERROR;
    if (!consume_keyword(&cursor, "table")) return PREPARE_SYNTAX_ERROR;
    if (!parse_identifier(&cursor, statement->table_name, sizeof(statement->table_name))) {
        return PREPARE_SYNTAX_ERROR;
    }
    return PREPARE_SUCCESS;
}

static PrepareResult prepare_select(const char *sql, Statement *statement) {
    statement->type = STATEMENT_SELECT;
    const char *cursor = sql;

    if (!consume_keyword(&cursor, "select")) return PREPARE_SYNTAX_ERROR;
    if (*cursor != '*') return PREPARE_SYNTAX_ERROR; // only "SELECT *" is supported
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;

    if (!consume_keyword(&cursor, "from")) return PREPARE_SYNTAX_ERROR;
    if (!parse_identifier(&cursor, statement->table_name, sizeof(statement->table_name))) {
        return PREPARE_SYNTAX_ERROR;
    }
    if (!parse_where_clause(cursor, &statement->where)) return PREPARE_SYNTAX_ERROR;
    return PREPARE_SUCCESS;
}

static PrepareResult prepare_insert(const char *sql, Statement *statement) {
    statement->type = STATEMENT_INSERT;
    const char *cursor = sql;

    if (!consume_keyword(&cursor, "insert")) return PREPARE_SYNTAX_ERROR;
    if (!consume_keyword(&cursor, "into")) return PREPARE_SYNTAX_ERROR;
    if (!parse_identifier(&cursor, statement->table_name, sizeof(statement->table_name))) {
        return PREPARE_SYNTAX_ERROR;
    }
    if (!consume_keyword(&cursor, "values")) return PREPARE_SYNTAX_ERROR;
    if (*cursor != '(') return PREPARE_SYNTAX_ERROR;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;

    char *end;
    long id = strtol(cursor, &end, 10);
    if (end == cursor) return PREPARE_SYNTAX_ERROR;
    if (id < 0) return PREPARE_NEGATIVE_ID;
    cursor = end;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor != ',') return PREPARE_SYNTAX_ERROR;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;

    LiteralResult username_result = parse_string_literal(
        &cursor, statement->row_to_insert.username, sizeof(statement->row_to_insert.username));
    if (username_result == LITERAL_SYNTAX_ERROR) return PREPARE_SYNTAX_ERROR;
    if (username_result == LITERAL_TOO_LONG) return PREPARE_STRING_TOO_LONG;

    if (*cursor != ',') return PREPARE_SYNTAX_ERROR;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;

    LiteralResult email_result = parse_string_literal(
        &cursor, statement->row_to_insert.email, sizeof(statement->row_to_insert.email));
    if (email_result == LITERAL_SYNTAX_ERROR) return PREPARE_SYNTAX_ERROR;
    if (email_result == LITERAL_TOO_LONG) return PREPARE_STRING_TOO_LONG;

    if (*cursor != ')') return PREPARE_SYNTAX_ERROR;

    statement->row_to_insert.id = (uint32_t)id;
    return PREPARE_SUCCESS;
}

static PrepareResult prepare_update(const char *sql, Statement *statement) {
    statement->type = STATEMENT_UPDATE;
    const char *cursor = sql;

    if (!consume_keyword(&cursor, "update")) return PREPARE_SYNTAX_ERROR;
    if (!parse_identifier(&cursor, statement->table_name, sizeof(statement->table_name))) {
        return PREPARE_SYNTAX_ERROR;
    }
    if (!consume_keyword(&cursor, "set")) return PREPARE_SYNTAX_ERROR;

    while (1) {
        bool is_username;
        if (consume_keyword(&cursor, "username")) {
            is_username = true;
        } else if (consume_keyword(&cursor, "email")) {
            is_username = false;
        } else {
            return PREPARE_SYNTAX_ERROR;
        }

        if (*cursor != '=') return PREPARE_SYNTAX_ERROR;
        cursor++;
        while (isspace((unsigned char)*cursor)) cursor++;

        char *target = is_username ? statement->row_to_update.username : statement->row_to_update.email;
        size_t target_size = is_username ? sizeof(statement->row_to_update.username)
                                          : sizeof(statement->row_to_update.email);
        LiteralResult lit = parse_string_literal(&cursor, target, target_size);
        if (lit == LITERAL_SYNTAX_ERROR) return PREPARE_SYNTAX_ERROR;
        if (lit == LITERAL_TOO_LONG) return PREPARE_STRING_TOO_LONG;

        if (is_username) statement->update_username_set = true;
        else statement->update_email_set = true;

        if (*cursor == ',') {
            cursor++;
            while (isspace((unsigned char)*cursor)) cursor++;
            continue;
        }
        break;
    }

    if (!parse_where_clause(cursor, &statement->where)) return PREPARE_SYNTAX_ERROR;
    if (!statement->where.has_where) return PREPARE_SYNTAX_ERROR; // UPDATE requires a WHERE clause

    return PREPARE_SUCCESS;
}

static PrepareResult prepare_delete(const char *sql, Statement *statement) {
    statement->type = STATEMENT_DELETE;
    const char *cursor = sql;

    if (!consume_keyword(&cursor, "delete")) return PREPARE_SYNTAX_ERROR;
    if (!consume_keyword(&cursor, "from")) return PREPARE_SYNTAX_ERROR;
    if (!parse_identifier(&cursor, statement->table_name, sizeof(statement->table_name))) {
        return PREPARE_SYNTAX_ERROR;
    }
    if (!parse_where_clause(cursor, &statement->where)) return PREPARE_SYNTAX_ERROR;
    if (!statement->where.has_where) return PREPARE_SYNTAX_ERROR; // DELETE requires a WHERE clause

    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(const char *sql, Statement *statement) {
    memset(statement, 0, sizeof(*statement));

    if (strncasecmp(sql, "select", 6) == 0) return prepare_select(sql, statement);
    if (strncasecmp(sql, "insert", 6) == 0) return prepare_insert(sql, statement);
    if (strncasecmp(sql, "update", 6) == 0) return prepare_update(sql, statement);
    if (strncasecmp(sql, "delete", 6) == 0) return prepare_delete(sql, statement);
    if (strncasecmp(sql, "create", 6) == 0) return prepare_create_table(sql, statement);
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

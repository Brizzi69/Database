#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <stdint.h>

#include "pager.h"

#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

// Must be plain macros (not "const uint32_t"): they size Row's arrays, and
// a non-macro const isn't a compile-time constant once it crosses a
// translation unit boundary, so the struct definition wouldn't compile
// anywhere outside the one file that also defines the const's value.
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

#define TABLE_NAME_SIZE 32
#define MAX_TABLES 100

// Every table shares this one fixed row shape -- there is no per-table
// schema, only per-table names/B-trees.
typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

// Plain macros for the same cross-translation-unit reason as above: these
// feed into btree.h's page-layout arithmetic, which must resolve to the
// same compile-time values in every file that includes it.
#define ID_SIZE (size_of_attribute(Row, id))
#define USERNAME_SIZE (size_of_attribute(Row, username))
#define EMAIL_SIZE (size_of_attribute(Row, email))
#define ID_OFFSET 0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);

typedef struct {
    Pager *pager;
} Database;

typedef struct {
    Pager *pager;
    uint32_t root_page_num;
} Table;

// Page 0 is reserved for this catalog; every table's own B-tree root lives
// on a separate page allocated when the table is created.
typedef struct {
    char name[TABLE_NAME_SIZE];
    uint32_t root_page_num;
} TableEntry;

typedef struct {
    uint32_t num_tables;
    TableEntry entries[MAX_TABLES];
} Catalog;

Database *db_open(const char *filename);
void db_close(Database *db);

bool db_create_table(Database *db, const char *name);
bool db_lookup_table(Database *db, const char *name, Table *out);

// Returns the catalog's table names into out_names (each up to
// TABLE_NAME_SIZE bytes), writing at most max_names entries. Returns the
// number of names written.
uint32_t db_list_tables(Database *db, char out_names[][TABLE_NAME_SIZE], uint32_t max_names);

typedef enum {
    DB_OK,
    DB_ERR_SYNTAX,
    DB_ERR_NO_SUCH_TABLE,
    DB_ERR_TABLE_EXISTS,
    DB_ERR_DUPLICATE_KEY,
    DB_ERR_NOT_FOUND,
    DB_ERR_INTERNAL,
} DbErrorCode;

// Mirrors the parser's internal statement type, so callers that only see
// database.h (not parser.h) can still tell what kind of statement ran.
typedef enum {
    DB_STMT_SELECT,
    DB_STMT_INSERT,
    DB_STMT_UPDATE,
    DB_STMT_DELETE,
    DB_STMT_CREATE_TABLE,
} DbStatementKind;

typedef struct {
    bool success;
    DbErrorCode error_code;
    char error_message[256];
    DbStatementKind statement_kind;

    // Populated for SELECT; NULL/0 otherwise. Caller must db_result_free().
    Row *rows;
    uint32_t row_count;

    // Populated for INSERT/UPDATE/DELETE.
    uint32_t rows_affected;
} DbResult;

// Parses and executes one SQL statement, e.g. "SELECT * FROM users;",
// "INSERT INTO users VALUES (1, 'ana', 'ana@example.com');",
// "UPDATE users SET email = 'x@y.com' WHERE id = 1;",
// "DELETE FROM users WHERE id = 1;", or "CREATE TABLE users;".
DbResult db_execute(Database *db, const char *sql);
void db_result_free(DbResult *result);

#endif

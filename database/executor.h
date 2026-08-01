#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "database.h"
#include "parser.h"

// Each executes an already-parsed, already-table-resolved statement,
// filling result (rows/row_count for SELECT, rows_affected for the rest).
// Internal to the library -- called only from database.c's db_execute.
void executor_select(const Statement *statement, Table *table, DbResult *result);
void executor_insert(const Statement *statement, Table *table, DbResult *result);
void executor_update(const Statement *statement, Table *table, DbResult *result);
void executor_delete(const Statement *statement, Table *table, DbResult *result);

#endif

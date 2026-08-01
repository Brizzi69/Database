# Database in C

A custom disk-persisted relational database engine built from scratch in C. This project implements a B-tree storage engine, a pager/buffer pool for disk I/O, a table catalog and a SQL-like query interface, packaged as a reusable library (`database/`) with a REPL (`main.c`) on top.

## Features

- **B-Tree Storage Engine:** Data is stored in a balanced B-Tree, ensuring $O(\log n)$ search, insert, and delete times.
- **Pager & Buffer Pool:** Reads and writes data in fixed 4KB pages to a binary file (`mydb.db`), handling cache misses and disk flushing.
- **Multi-Table Support:** Page 0 is reserved for a Table Catalog, allowing the creation of multiple independent tables (e.g., `users`, `products`), each with its own B-Tree. Every table shares the same fixed row shape (`id`, `username`, `email`) — there's no per-table schema.
- **SQL Syntax:** Supports `CREATE TABLE`, `INSERT INTO ... VALUES (...)`, `SELECT * FROM ...`, `UPDATE ... SET ...`, and `DELETE FROM ...`. Keywords are case-insensitive; the trailing `;` is optional.
- **WHERE Clause Filtering:** Supports filtering rows by `id`, `username`, or `email`.
- **Leaf Node Sibling Pointers:** Leaf nodes are linked together, allowing the cursor to efficiently scan across multiple pages during a full table scan.
- **Data Validation:** Rejects negative/non-numeric IDs, duplicate primary keys, and `username`/`email` values that overflow their fixed column sizes (32 / 255 bytes), all without corrupting the tree.
- **Library API:** `db_open`/`db_execute`/`db_close` return structured results (`DbResult`), so other programs (e.g. the HTTP server in the sibling project) can embed this engine instead of only driving it through the REPL.

## Project Layout

```
database/               # repo root
├── main.c              # REPL entry point (uses database/database.h only)
└── database/           # the library
    ├── database.h/.c    # public API: db_open, db_execute, db_close, Row/DbResult
    ├── pager.h/.c        # fixed-size page I/O and caching
    ├── btree.h/.c        # B-tree node layout and traversal
    ├── cursor.h/.c       # table scan abstraction over the B-tree
    ├── parser.h/.c       # SQL text -> Statement
    └── executor.h/.c     # Statement -> DbResult (internal to the library)
```

## How to Compile and Run

### Prerequisites
- A C compiler (like `gcc` or `clang`)

### Compilation
```
gcc -std=c11 -Wall -Wextra -Idatabase -o SQL main.c database/*.c
```

### Run
```
./SQL mydb.db
```

(You can replace "mydb.db" with whatever you want to name your database file. If it doesn't exist it will be created automatically)

### Usage Examples
Once inside the REPL (`db >`), you can use the following commands:

### Meta-Commands
`.tables` : List all tables in the database.

`.btree` `table_name` : Print the internal B-Tree structure of a specific table (useful for debugging).

`.exit` : Save changes to disk and exit.

### SQL Commands
```sql
-- Create a new table
CREATE TABLE users;

-- Insert data
INSERT INTO users VALUES (1, 'alice', 'alice@example.com');
INSERT INTO users VALUES (2, 'bob', 'bob@example.com');

-- Select data
SELECT * FROM users;
SELECT * FROM users WHERE id = 1;
SELECT * FROM users WHERE username = 'alice';

-- Update data (WHERE is required)
UPDATE users SET username = 'alice_updated' WHERE id = 1;
UPDATE users SET username = 'alice2', email = 'alice2@example.com' WHERE id = 1;

-- Delete data (WHERE is required)
DELETE FROM users WHERE id = 2;
```

## Using It as a Library

Any C program can embed this engine instead of driving it through the REPL:

```c
#include "database/database.h"

Database *db = db_open("mydb.db");

DbResult result = db_execute(db, "SELECT * FROM users;");
if (result.success) {
    for (uint32_t i = 0; i < result.row_count; i++) {
        printf("%d %s %s\n", result.rows[i].id, result.rows[i].username, result.rows[i].email);
    }
}
db_result_free(&result);

db_close(db);
```

This is exactly how the sibling `HTTP server` project's REST API (`GET/POST/PUT/DELETE /api/<table>`) is backed by this engine.

## Architecture
1. **The Pager** (`pager.c`): The lowest level of the database. It abstracts the file system, reading and writing fixed 4KB pages into memory.

2. **The Table Catalog** (`database.c`): Stored on Page 0. It keeps track of table names and the root page number of their respective B-Trees.

3. **The B-Tree** (`btree.c`):
Internal Nodes: Store keys and child page pointers to guide searches down the tree.
Leaf Nodes: Store the actual data (Key-Value pairs, where Key = ID, Value = Serialized Row).
Leaf nodes contain a next_leaf pointer to allow sequential scanning.

4. **The Cursor** (`cursor.c`): An abstraction used to traverse the B-Tree and read/write rows without needing to know the underlying page structure.

5. **The Parser** (`parser.c`): Turns SQL text into a `Statement` struct.

6. **The Executor** (`executor.c`): Runs a parsed `Statement` against a `Table`, filling in a `DbResult` (rows for SELECT, rows-affected for INSERT/UPDATE/DELETE) instead of printing directly — that's what makes this usable as a library and not just a REPL.

## Challenges & Issues Faced
Building a database from scratch came with several complex debugging hurdles:

1. The B-Tree Splitting Infinite Loop:
When a leaf node filled up and split, converting the root into an internal node caused memory corruption. The internal node's num_keys and keys were reading garbage memory. This caused internal_node_find to calculate an out-of-bounds child_index, resulting in an infinite loop where the cursor kept checking Page 0 instead of moving down the tree. Fix: Properly initializing the internal node structure and zeroing out memory before setting keys.

2. Cursor Traversal Across Pages:
Initially, a SELECT command would only read the first leaf node it landed on. If the table spanned multiple pages, the rest of the data was invisible. Fix: Added a next_leaf pointer to the leaf node header and updated cursor_advance to follow these sibling pointers when reaching the end of a page.

3. Script EOF Handling:
When piping SQL commands via a bash script, getline would fail at the end of the file and call exit(), bypassing the db_close() function. This meant data was never flushed to disk. Fix: Changed read_input to return a boolean, allowing the main loop to break gracefully and trigger the cleanup/flush sequence.

4. Splitting a Single File Into a Library:
Several `const uint32_t` values (e.g. `PAGE_SIZE`, `ROW_SIZE`, and the whole B-tree node layout) were originally computed as a chain of file-scope `const` variables, each initialized from the previous one. That only worked by accident in a single translation unit — an object initializer must be a compile-time constant expression in C, and a `const` from another `.c` file (via `extern`) doesn't qualify, so the moment these were split across files the struct/array definitions that depended on them stopped compiling ("initializer element is not a compile-time constant"). Fix: converted the whole derived-constant chain to `#define` macros, which the preprocessor resolves identically in every file that includes the header, with no cross-file linkage involved.

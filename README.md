# Mini-Database in C

A custom disk-persisted relational database engine built from scratch in C. This project implements a B-tree storage engine, a pager/buffer pool for disk I/O, a table catalog and a SQL-like query interface. 

## Features

- **B-Tree Storage Engine:** Data is stored in a balanced B-Tree, ensuring $O(\log n)$ search, insert, and delete times.
- **Pager & Buffer Pool:** Reads and writes data in fixed 4KB pages to a binary file (`mydb.db`), handling cache misses and disk flushing.
- **Multi-Table Support:** Page 0 is reserved for a Table Catalog, allowing the creation of multiple independent tables (e.g., `users`, `products`), each with its own B-Tree.
- **SQL-Like Commands:** Supports `CREATE TABLE`, `INSERT`, `SELECT`, `UPDATE`, and `DELETE`.
- **WHERE Clause Filtering:** Supports filtering rows by `id`, `username`, or `email`.
- **Leaf Node Sibling Pointers:** Leaf nodes are linked together, allowing the cursor to efficiently scan across multiple pages during a full table scan.

## How to Compile and Run

### Prerequisites
- A C compiler (like `gcc` or `clang`)

### Compilation
gcc SQL.c -o SQL

### Run 
./SQL mydb.db

(You can replace "mydb.db" with whatever you want to name your database file. If it doesn't exist it will be created automatically)

### Usage Examples
Once inside the REPL (db >), you can use the following commands:

### Meta-Commands
`.tables` : List all tables in the database.

`.btree` `table_name` : Print the internal B-Tree structure of a specific table (useful for debugging).

`.exit` : Save changes to disk and exit.

### SQL Commands
-- Create a new table
create table users

-- Insert data (Syntax: insert `table` `id` `username` `email`)

insert users 1 alice alice@example.com

insert users 2 bob bob@example.com


-- Select data (Syntax: select `table` `[WHERE <column> = <value>]`)

select users

select users WHERE id = 1

select users WHERE username = 'alice'


-- Update data (Syntax: update `table` `SET <col> = '<val>' WHERE <col> = <val>`)

update users SET username = 'alice_updated' WHERE id = 1


-- Delete data (Syntax: delete `table` `WHERE <col> = <val>`)

delete users WHERE id = 2

## Architecture
1. The Pager: The lowest level of the database. It abstracts the file system, reading and writing fixed 4KB pages into memory.

2. The Table Catalog: Stored on Page 0. It keeps track of table names and the root page number of their respective B-Trees.

3. The B-Tree:
Internal Nodes: Store keys and child page pointers to guide searches down the tree.
Leaf Nodes: Store the actual data (Key-Value pairs, where Key = ID, Value = Serialized Row).
Leaf nodes contain a next_leaf pointer to allow sequential scanning.

5. The Cursor: An abstraction used by the Virtual Machine to traverse the B-Tree and read/write rows without needing to know the underlying page structure.

## Challenges & Issues Faced
Building a database from scratch came with several complex debugging hurdles:

1. The B-Tree Splitting Infinite Loop:
When a leaf node filled up and split, converting the root into an internal node caused memory corruption. The internal node's num_keys and keys were reading garbage memory. This caused internal_node_find to calculate an out-of-bounds child_index, resulting in an infinite loop where the cursor kept checking Page 0 instead of moving down the tree. Fix: Properly initializing the internal node structure and zeroing out memory before setting keys.

2. Cursor Traversal Across Pages:
Initially, a SELECT command would only read the first leaf node it landed on. If the table spanned multiple pages, the rest of the data was invisible. Fix: Added a next_leaf pointer to the leaf node header and updated cursor_advance to follow these sibling pointers when reaching the end of a page.

3. Script EOF Handling:
When piping SQL commands via a bash script, getline would fail at the end of the file and call exit(), bypassing the db_close() function. This meant data was never flushed to disk. Fix: Changed read_input to return a boolean, allowing the main loop to break gracefully and trigger the cleanup/flush sequence.
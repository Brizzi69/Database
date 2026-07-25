#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

// constans and macros
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

const uint32_t COLUMN_USERNAME_SIZE = 32;
const uint32_t COLUMN_EMAIL_SIZE = 255;
#define TABLE_NAME_SIZE 32
#define MAX_TABLES 100

typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;
#define TABLE_MAX_PAGES 1000

// Node header constants
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// Leaf header constants
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET = LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;

const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

// Internal node constants
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET = INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE;

const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE = INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
const uint32_t INTERNAL_NODE_SPACE_FOR_CELLS = PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_MAX_CELLS = INTERNAL_NODE_SPACE_FOR_CELLS / INTERNAL_NODE_CELL_SIZE;

// Enums
enum NodeType { NODE_LEAF, NODE_INTERNAL };

// Serialization
void serialize_row(Row* source, void* destination) {
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  strncpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
  strncpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

// Data structures
typedef struct {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void* pages[TABLE_MAX_PAGES];
} Pager;

// Page 0 is reserved for the table catalog; every table's own B-tree root
// lives on a separate page allocated when the table is created.
typedef struct {
  char name[TABLE_NAME_SIZE];
  uint32_t root_page_num;
} TableEntry;

typedef struct {
  uint32_t num_tables;
  TableEntry entries[MAX_TABLES];
} Catalog;

typedef struct {
  Pager* pager;
} Database;

typedef struct {
  Pager* pager;
  uint32_t root_page_num;
} Table;

typedef struct {
  Table* table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table;
} Cursor;

// Node accessor functions
uint8_t* node_type(void* node) { return node + NODE_TYPE_OFFSET; }

bool is_node_root(void* node) {
  uint8_t value = *(uint8_t*)(node + IS_ROOT_OFFSET);
  return (bool)value;
}

void set_node_root(void* node, bool is_root) {
  uint8_t value = is_root;
  *(uint8_t*)(node + IS_ROOT_OFFSET) = value;
}

uint32_t* node_parent(void* node) { return node + PARENT_POINTER_OFFSET; }

uint32_t* leaf_node_num_cells(void* node) {
  return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

uint32_t* leaf_node_next_leaf(void* node) {
  return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_OFFSET;
}

void* leaf_node_value(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_VALUE_OFFSET;
}

void initialize_leaf_node(void* node) {
  *leaf_node_num_cells(node) = 0;
  *leaf_node_next_leaf(node) = 0;
}

// Internal node accessor
uint32_t* internal_node_num_keys(void* node) {
  return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t* internal_node_right_child(void* node) {
  return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

void* internal_node_cell(void* node, uint32_t cell_num) {
  return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t* internal_node_child(void* node, uint32_t child_num) {
  uint32_t num_keys = *internal_node_num_keys(node);
  if (child_num > num_keys) {
    printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
    exit(EXIT_FAILURE);
  } else if (child_num == num_keys) {
    return internal_node_right_child(node);
  } else {
    return internal_node_cell(node, child_num);
  }
}

uint32_t* internal_node_key(void* node, uint32_t key_num) {
  return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

void initialize_internal_node(void* node) {
  *internal_node_num_keys(node) = 0;
}

// Pager and table functions
void* get_page(Pager* pager, uint32_t page_num) {
  if (page_num > TABLE_MAX_PAGES) {
    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    void* page = malloc(PAGE_SIZE);
    uint32_t num_pages = pager->file_length / PAGE_SIZE;

    if (pager->file_length % PAGE_SIZE) {
      num_pages += 1;
    }

    if (page_num <= num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;
  }

  return pager->pages[page_num];
}

uint32_t allocate_page(Pager* pager) {
  void* page = get_page(pager, pager->num_pages);
  return pager->num_pages++;
}

Pager* pager_open(const char* filename) {
  int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
  if (fd == -1) {
    printf("Unable to open file %s\n", filename);
    exit(EXIT_FAILURE);
  }

  off_t file_length = lseek(fd, 0, SEEK_END);

  Pager* pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = file_length;
  pager->num_pages = (file_length / PAGE_SIZE);
  if (file_length % PAGE_SIZE != 0) {
    pager->num_pages += 1;
  }

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = NULL;
  }

  return pager;
}

Database* db_open(const char* filename) {
  Pager* pager = pager_open(filename);

  Database* db = malloc(sizeof(Database));
  db->pager = pager;

  if (pager->num_pages == 0) {
    void* catalog_page = get_page(pager, 0);
    memset(catalog_page, 0, PAGE_SIZE);
    Catalog* catalog = (Catalog*)catalog_page;
    catalog->num_tables = 0;
    pager->num_pages += 1;
  }

  return db;
}

void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
  if (pager->pages[page_num] == NULL) {
    printf("Tried to flush null page\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
  if (offset == -1) {
    printf("Error seeking: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written = write(pager->file_descriptor, pager->pages[page_num], size);
  if (bytes_written == -1) {
    printf("Error writing: %d\n", errno);
    exit(EXIT_FAILURE);
  }
}

void db_close(Database* db) {
  Pager* pager = db->pager;

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

// Table catalog
bool db_create_table(Database* db, const char* name) {
  Catalog* catalog = (Catalog*)get_page(db->pager, 0);

  for (uint32_t i = 0; i < catalog->num_tables; i++) {
    if (strcmp(catalog->entries[i].name, name) == 0) {
      return false;
    }
  }
  if (catalog->num_tables >= MAX_TABLES) {
    return false;
  }

  uint32_t root_page_num = allocate_page(db->pager);
  void* root = get_page(db->pager, root_page_num);
  *node_type(root) = NODE_LEAF;
  set_node_root(root, true);
  initialize_leaf_node(root);
  *node_parent(root) = 0;

  strncpy(catalog->entries[catalog->num_tables].name, name, TABLE_NAME_SIZE - 1);
  catalog->entries[catalog->num_tables].name[TABLE_NAME_SIZE - 1] = '\0';
  catalog->entries[catalog->num_tables].root_page_num = root_page_num;
  catalog->num_tables += 1;

  return true;
}

bool db_lookup_table(Database* db, const char* name, Table* out) {
  Catalog* catalog = (Catalog*)get_page(db->pager, 0);

  for (uint32_t i = 0; i < catalog->num_tables; i++) {
    if (strcmp(catalog->entries[i].name, name) == 0) {
      out->pager = db->pager;
      out->root_page_num = catalog->entries[i].root_page_num;
      return true;
    }
  }
  return false;
}

// B-tree functions
uint32_t leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_cells;

  while (one_past_max_index != min_index) {
    uint32_t index = (min_index + one_past_max_index) / 2;
    uint32_t key_at_index = *leaf_node_key(node, index);

    if (key == key_at_index) {
      return index;
    }
    if (key < key_at_index) {
      one_past_max_index = index;
    } else {
      min_index = index + 1;
    }
  }
  return min_index;
}

uint32_t internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_keys = *internal_node_num_keys(node);

  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_keys;

  while (one_past_max_index != min_index) {
    uint32_t index = (min_index + one_past_max_index) / 2;
    uint32_t key_to_right = *internal_node_key(node, index);

    if (key_to_right >= key) {
      one_past_max_index = index;
    } else {
      min_index = index + 1;
    }
  }
  return min_index;
}

uint32_t get_node_max_key(void* node) {
  if (*node_type(node) == NODE_LEAF) {
    return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
  return *internal_node_key(node, *internal_node_num_keys(node) - 1);
}

// Finds the leaf page that a key belongs in (or would be inserted into),
// walking down through however many internal-node levels exist.
uint32_t table_find_leaf(Table* table, uint32_t key) {
  uint32_t page_num = table->root_page_num;
  void* node = get_page(table->pager, page_num);

  while (*node_type(node) != NODE_LEAF) {
    uint32_t child_index = internal_node_find(table, page_num, key);
    page_num = *internal_node_child(node, child_index);
    node = get_page(table->pager, page_num);
  }

  return page_num;
}

void update_internal_node_key(Table* table, uint32_t node_page_num, uint32_t old_key, uint32_t new_key) {
  void* node = get_page(table->pager, node_page_num);
  uint32_t old_child_index = internal_node_find(table, node_page_num, old_key);
  *internal_node_key(node, old_child_index) = new_key;
}

// Inserts a newly-created child page into a parent internal node. The
// parent is assumed to have room; if it's already full this is a tree
// depth beyond what this implementation supports (see leaf_node_split_and_insert).
void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
  void* parent = get_page(table->pager, parent_page_num);
  void* child = get_page(table->pager, child_page_num);
  uint32_t child_max_key = get_node_max_key(child);
  uint32_t index = internal_node_find(table, parent_page_num, child_max_key);

  uint32_t original_num_keys = *internal_node_num_keys(parent);

  if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
    printf("Error: internal node is full. Splitting internal nodes is not yet supported.\n");
    exit(EXIT_FAILURE);
  }

  uint32_t right_child_page_num = *internal_node_right_child(parent);
  void* right_child = get_page(table->pager, right_child_page_num);

  if (child_max_key > get_node_max_key(right_child)) {
    // Write directly to the raw cell rather than through internal_node_child():
    // that accessor redirects child_num == num_keys to the right_child field,
    // which is exactly the (not yet bumped) index used here.
    *(uint32_t*)internal_node_cell(parent, original_num_keys) = right_child_page_num;
    *internal_node_key(parent, original_num_keys) = get_node_max_key(right_child);
    *internal_node_right_child(parent) = child_page_num;
  } else {
    for (uint32_t i = original_num_keys; i > index; i--) {
      void* destination = internal_node_cell(parent, i);
      void* source = internal_node_cell(parent, i - 1);
      memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
    }
    *internal_node_child(parent, index) = child_page_num;
    *internal_node_key(parent, index) = child_max_key;
  }

  *internal_node_num_keys(parent) = original_num_keys + 1;
  *node_parent(child) = parent_page_num;
}

// Cursor functions
Cursor* table_start(Table* table) {
  uint32_t page_num = table->root_page_num;
  void* node = get_page(table->pager, page_num);

  while (*node_type(node) != NODE_LEAF) {
    uint32_t child_page_num = *internal_node_child(node, 0);
    page_num = child_page_num;
    node = get_page(table->pager, page_num);
  }

  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;
  cursor->cell_num = 0;
  uint32_t num_cells = *leaf_node_num_cells(node);
  cursor->end_of_table = (num_cells == 0);
  return cursor;
}

Cursor* table_end(Table* table) {
  uint32_t page_num = table->root_page_num;
  void* node = get_page(table->pager, page_num);

  while (*node_type(node) != NODE_LEAF) {
    uint32_t child_page_num = *internal_node_right_child(node);
    page_num = child_page_num;
    node = get_page(table->pager, page_num);
  }

  uint32_t num_cells = *leaf_node_num_cells(node);
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;
  cursor->cell_num = num_cells;
  cursor->end_of_table = true;
  return cursor;
}

void cursor_advance(Cursor* cursor) {
  void* node = get_page(cursor->table->pager, cursor->page_num);

  cursor->cell_num += 1;
  if (cursor->cell_num >= *leaf_node_num_cells(node)) {
    uint32_t next_leaf = *leaf_node_next_leaf(node);
    if (next_leaf == 0) {
      cursor->end_of_table = true;
    } else {
      cursor->page_num = next_leaf;
      cursor->cell_num = 0;
    }
  }
}

void* cursor_value(Cursor* cursor) {
  void* page = get_page(cursor->table->pager, cursor->page_num);
  return leaf_node_value(page, cursor->cell_num);
}

// Repl and input buffer
typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;
  return input_buffer;
}

void print_prompt() { printf("db > "); }

bool read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read < 0) {
    return false;
  }

  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
  return true;
}

void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

// Enums and statements
typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND,
  META_COMMAND_BTREE
} MetaCommandResult;

typedef enum { PREPARE_SUCCESS, PREPARE_UNRECOGNIZED_STATEMENT, PREPARE_SYNTAX_ERROR, PREPARE_STRING_TOO_LONG, PREPARE_NEGATIVE_ID } PrepareResult;

// New: extended statement types
typedef enum {
  STATEMENT_CREATE_TABLE,
  STATEMENT_INSERT,
  STATEMENT_SELECT,
  STATEMENT_DELETE,
  STATEMENT_UPDATE
} StatementType;

// New: column types for WHERE clause
typedef enum {
  COLUMN_ID,
  COLUMN_USERNAME,
  COLUMN_EMAIL
} ColumnType;

// New: where clause structure
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
  // For UPDATE
  Row row_to_update;
} Statement;

// Meta command
void print_indented(uint32_t level) {
  for (uint32_t i = 0; i < level; i++) printf("  ");
}

void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level) {
  void* node = get_page(pager, page_num);
  uint32_t num_keys;

  if (*node_type(node) == NODE_LEAF) {
    num_keys = *leaf_node_num_cells(node);
    print_indented(indentation_level);
    printf("- leaf (size %d)\n", num_keys);
    for (uint32_t i = 0; i < num_keys; i++) {
      print_indented(indentation_level + 1);
      printf("- %d\n", *leaf_node_key(node, i));
    }
  } else {
    num_keys = *internal_node_num_keys(node);
    print_indented(indentation_level);
    printf("- internal (size %d)\n", num_keys);
    for (uint32_t i = 0; i < num_keys; i++) {
      uint32_t child = *internal_node_child(node, i);
      print_tree(pager, child, indentation_level + 1);
      print_indented(indentation_level + 1);
      printf("- key %d\n", *internal_node_key(node, i));
    }
    uint32_t right_child = *internal_node_right_child(node);
    print_tree(pager, right_child, indentation_level + 1);
  }
}

void print_tables(Database* db) {
  Catalog* catalog = (Catalog*)get_page(db->pager, 0);
  if (catalog->num_tables == 0) {
    printf("(no tables)\n");
    return;
  }
  for (uint32_t i = 0; i < catalog->num_tables; i++) {
    printf("%s\n", catalog->entries[i].name);
  }
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer, Database* db) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    db_close(db);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".tables") == 0) {
    print_tables(db);
    return META_COMMAND_SUCCESS;
  } else if (strncmp(input_buffer->buffer, ".btree", 6) == 0) {
    char* name = input_buffer->buffer + 6;
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

// Compile

// Extracts the first whitespace-delimited token after the statement keyword
// (the table name) without mutating the input buffer, so later parsing
// (e.g. WHERE-clause scanning) can still see the full original line.
bool parse_table_name(char* input, char* out, bool require_trailing) {
  char* space = strchr(input, ' ');
  if (space == NULL) return false;
  char* name_start = space + 1;
  while (*name_start == ' ') name_start++;
  char* name_end = strchr(name_start, ' ');
  if (require_trailing && name_end == NULL) return false;
  size_t name_len = name_end ? (size_t)(name_end - name_start) : strlen(name_start);
  if (name_len == 0 || name_len >= TABLE_NAME_SIZE) return false;
  strncpy(out, name_start, name_len);
  out[name_len] = '\0';
  return true;
}

PrepareResult prepare_create_table(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_CREATE_TABLE;
  char* keyword = strtok(input_buffer->buffer, " ");
  char* table_kw = strtok(NULL, " ");
  char* name = strtok(NULL, " ");
  (void)keyword;

  if (table_kw == NULL || strcmp(table_kw, "table") != 0 || name == NULL) {
    return PREPARE_SYNTAX_ERROR;
  }
  if (strlen(name) >= TABLE_NAME_SIZE) return PREPARE_STRING_TOO_LONG;

  strcpy(statement->table_name, name);
  return PREPARE_SUCCESS;
}

PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_INSERT;
  char* keyword = strtok(input_buffer->buffer, " ");
  char* table_name = strtok(NULL, " ");
  char* id_string = strtok(NULL, " ");
  char* username = strtok(NULL, " ");
  char* email = strtok(NULL, " ");
  (void)keyword;

  if (table_name == NULL || id_string == NULL || username == NULL || email == NULL) return PREPARE_SYNTAX_ERROR;
  if (strlen(table_name) >= TABLE_NAME_SIZE) return PREPARE_STRING_TOO_LONG;
  int id = atoi(id_string);
  if (id < 0) return PREPARE_NEGATIVE_ID;
  if (strlen(username) > COLUMN_USERNAME_SIZE) return PREPARE_STRING_TOO_LONG;
  if (strlen(email) > COLUMN_EMAIL_SIZE) return PREPARE_STRING_TOO_LONG;

  strcpy(statement->table_name, table_name);
  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);
  return PREPARE_SUCCESS;
}

// New: parse WHERE clause
bool parse_where_clause(char* input, WhereClause* where) {
  char* where_pos = strstr(input, " WHERE ");
  if (where_pos == NULL) {
    where->has_where = false;
    return true;
  }

  where->has_where = true;
  where_pos += 7; // Skip "WHERE"

  // Parse column name
  if (strncmp(where_pos, "id = ", 5) == 0) {
    where->column = COLUMN_ID;
    where->id_value = atoi(where_pos + 5);
    sprintf(where->value, "%u", where->id_value);
  } else if (strncmp(where_pos, "username = '", 12) == 0) {
    where->column = COLUMN_USERNAME;
    char* end_quote = strchr(where_pos + 12, '\'');
    if (end_quote == NULL) return false;
    strncpy(where->value, where_pos + 12, end_quote - (where_pos + 12));
    where->value[end_quote - (where_pos + 12)] = '\0';
  } else if (strncmp(where_pos, "email = '", 9) == 0) {
    where->column = COLUMN_EMAIL;
    char* end_quote = strchr(where_pos + 9, '\'');
    if (end_quote == NULL) return false;
    strncpy(where->value, where_pos + 9, end_quote - (where_pos + 9));
    where->value[end_quote - (where_pos + 9)] = '\0';
  } else {
    return false;
  }

  return true;
}

// New: check if a row matches the WHERE clause
bool row_matches_where(Row* row, WhereClause* where) {
  if (!where->has_where) return true;

  switch (where->column) {
    case COLUMN_ID:
      return row->id == where->id_value;
    case COLUMN_USERNAME:
      return strcmp(row->username, where->value) == 0;
    case COLUMN_EMAIL:
      return strcmp(row->email, where->value) == 0;
  }
  return false;
}

PrepareResult prepare_select(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_SELECT;

  if (!parse_table_name(input_buffer->buffer, statement->table_name, false)) {
    return PREPARE_SYNTAX_ERROR;
  }
  if (!parse_where_clause(input_buffer->buffer, &statement->where)) {
    return PREPARE_SYNTAX_ERROR;
  }
  return PREPARE_SUCCESS;
}

PrepareResult prepare_delete(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_DELETE;

  if (!parse_table_name(input_buffer->buffer, statement->table_name, true)) {
    return PREPARE_SYNTAX_ERROR;
  }
  if (!parse_where_clause(input_buffer->buffer, &statement->where)) {
    return PREPARE_SYNTAX_ERROR;
  }
  // "DELETE" requires a "WHERE" clause
  if (!statement->where.has_where) {
    return PREPARE_SYNTAX_ERROR;
  }
  return PREPARE_SUCCESS;
}

PrepareResult prepare_update(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_UPDATE;
  statement->row_to_update.username[0] = '\0';
  statement->row_to_update.email[0] = '\0';

  if (!parse_table_name(input_buffer->buffer, statement->table_name, true)) {
    return PREPARE_SYNTAX_ERROR;
  }

  // Parse example update users SET username = 'newname' email = 'new@email.com' WHERE id = 15
  char* set_pos = strstr(input_buffer->buffer, " SET ");
  if (set_pos == NULL) return PREPARE_SYNTAX_ERROR;

  // Parse WHERE clause, must come after SET
  char* where_pos = strstr(set_pos, " WHERE ");
  if (where_pos == NULL) return PREPARE_SYNTAX_ERROR;

  if (!parse_where_clause(input_buffer->buffer, &statement->where)) {
    return PREPARE_SYNTAX_ERROR;
  }

  // Parse SET values
  char* set_values = set_pos + 5;
  char* username_start = strstr(set_values, "username = '");
  char* email_start = strstr(set_values, "email = '");

  if (username_start) {
    username_start += 12;
    char* end_quote = strchr(username_start, '\'');
    if (end_quote) {
      strncpy(statement->row_to_update.username, username_start, end_quote - username_start);
      statement->row_to_update.username[end_quote - username_start] = '\0';
    }
  }

  if (email_start) {
    email_start += 9;
    char* end_quote = strchr(email_start, '\'');
    if (end_quote) {
      strncpy(statement->row_to_update.email, email_start, end_quote - email_start);
      statement->row_to_update.email[end_quote - email_start] = '\0';
    }
  }

  return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
  if (strncmp(input_buffer->buffer, "create", 6) == 0) {
    return prepare_create_table(input_buffer, statement);
  }
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement);
  }
  if (strncmp(input_buffer->buffer, "select", 6) == 0) {
    return prepare_select(input_buffer, statement);
  }
  if (strncmp(input_buffer->buffer, "delete", 6) == 0) {
    return prepare_delete(input_buffer, statement);
  }
  if (strncmp(input_buffer->buffer, "update", 6) == 0) {
    return prepare_update(input_buffer, statement);
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}

// Virtual machine
void print_row(Row* row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void create_new_root(Table* table, uint32_t right_child_page_num) {
  void* root = get_page(table->pager, table->root_page_num);
  void* right_child = get_page(table->pager, right_child_page_num);
  uint32_t left_child_page_num = allocate_page(table->pager);
  void* left_child = get_page(table->pager, left_child_page_num);

  memcpy(left_child, root, PAGE_SIZE);
  set_node_root(left_child, false);

  *node_type(root) = NODE_INTERNAL;
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
  *node_parent(left_child) = table->root_page_num;
  *node_parent(right_child) = table->root_page_num;
}

void leaf_node_split_and_insert(Table* table, uint32_t page_num, uint32_t key, Row* value) {
  void* old_node = get_page(table->pager, page_num);
  uint32_t old_num_cells = *leaf_node_num_cells(old_node);
  uint32_t old_max = *leaf_node_key(old_node, old_num_cells - 1);
  uint32_t split_boundary_key = *leaf_node_key(old_node, LEAF_NODE_LEFT_SPLIT_COUNT - 1);

  uint32_t new_page_num = allocate_page(table->pager);
  void* new_node = get_page(table->pager, new_page_num);
  initialize_leaf_node(new_node);
  *node_type(new_node) = NODE_LEAF;
  set_node_root(new_node, false);
  *node_parent(new_node) = *node_parent(old_node);
  *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
  *leaf_node_next_leaf(old_node) = new_page_num;

  uint32_t num_moved = old_num_cells - LEAF_NODE_LEFT_SPLIT_COUNT;
  for (uint32_t i = 0; i < num_moved; i++) {
    void* destination = leaf_node_cell(new_node, i);
    void* source = leaf_node_cell(old_node, i + LEAF_NODE_LEFT_SPLIT_COUNT);
    memcpy(destination, source, LEAF_NODE_CELL_SIZE);
  }

  *leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
  *leaf_node_num_cells(new_node) = num_moved;

  uint32_t index_to_insert;
  void* node_to_insert;
  uint32_t page_to_insert;

  if (key <= split_boundary_key) {
    node_to_insert = old_node;
    page_to_insert = page_num;
  } else {
    node_to_insert = new_node;
    page_to_insert = new_page_num;
  }

  uint32_t num_cells = *leaf_node_num_cells(node_to_insert);
  index_to_insert = leaf_node_find(table, page_to_insert, key);

  for (uint32_t i = num_cells; i > index_to_insert; i--) {
    void* destination = leaf_node_cell(node_to_insert, i);
    void* source = leaf_node_cell(node_to_insert, i - 1);
    memcpy(destination, source, LEAF_NODE_CELL_SIZE);
  }

  *leaf_node_key(node_to_insert, index_to_insert) = key;
  serialize_row(value, leaf_node_value(node_to_insert, index_to_insert));
  *leaf_node_num_cells(node_to_insert) = num_cells + 1;

  if (is_node_root(old_node)) {
    create_new_root(table, new_page_num);
  } else {
    uint32_t parent_page_num = *node_parent(old_node);
    update_internal_node_key(table, parent_page_num, old_max, split_boundary_key);
    internal_node_insert(table, parent_page_num, new_page_num);
  }
}

void execute_create_table(Statement* statement, Database* db) {
  if (db_create_table(db, statement->table_name)) {
    printf("Table '%s' created.\n", statement->table_name);
  } else {
    printf("Error: table '%s' already exists or table limit reached.\n", statement->table_name);
  }
}

void execute_insert(Statement* statement, Table* table) {
  Row* row_to_insert = &(statement->row_to_insert);
  uint32_t key_to_insert = row_to_insert->id;

  uint32_t leaf_page_num = table_find_leaf(table, key_to_insert);
  void* node = get_page(table->pager, leaf_page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  uint32_t index_to_insert = leaf_node_find(table, leaf_page_num, key_to_insert);

  if (index_to_insert < num_cells) {
    uint32_t key_at_index = *leaf_node_key(node, index_to_insert);
    if (key_at_index == key_to_insert) {
      printf("Error: Duplicate key.\n");
      return;
    }
  }

  if (num_cells >= LEAF_NODE_MAX_CELLS) {
    leaf_node_split_and_insert(table, leaf_page_num, key_to_insert, row_to_insert);
    printf("Executed.\n");
    return;
  }

  for (uint32_t i = num_cells; i > index_to_insert; i--) {
    void* destination = leaf_node_cell(node, i);
    void* source = leaf_node_cell(node, i - 1);
    memcpy(destination, source, LEAF_NODE_CELL_SIZE);
  }

  *leaf_node_key(node, index_to_insert) = key_to_insert;
  serialize_row(row_to_insert, leaf_node_value(node, index_to_insert));
  *leaf_node_num_cells(node) = num_cells + 1;

  printf("Executed.\n");
}

// New: execute SELECT with WHERE filtering
void execute_select(Statement* statement, Table* table) {
  Cursor* cursor = table_start(table);
  Row row;
  uint32_t count = 0;

  while (!(cursor->end_of_table)) {
    deserialize_row(cursor_value(cursor), &row);

    // Only print if it matches the WHERE clause
    if (row_matches_where(&row, &statement->where)) {
      print_row(&row);
      count++;
    }

    cursor_advance(cursor);
  }

  free(cursor);
  printf("%d row(s) returned.\n", count);
}

// New: execute DELETE with WHERE (scans every leaf via sibling pointers)
void execute_delete(Statement* statement, Table* table) {
  uint32_t deleted_count = 0;
  Cursor* cursor = table_start(table);
  Row row;

  while (!(cursor->end_of_table)) {
    void* node = get_page(table->pager, cursor->page_num);
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
        void* dest = leaf_node_cell(node, j);
        void* src = leaf_node_cell(node, j + 1);
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
  printf("%d row(s) deleted.\n", deleted_count);
}

// New: execute UPDATE with WHERE (scans every leaf via sibling pointers)
void execute_update(Statement* statement, Table* table) {
  uint32_t updated_count = 0;
  Cursor* cursor = table_start(table);
  Row row;

  while (!(cursor->end_of_table)) {
    void* node = get_page(table->pager, cursor->page_num);
    deserialize_row(leaf_node_value(node, cursor->cell_num), &row);

    if (row_matches_where(&row, &statement->where)) {
      if (strlen(statement->row_to_update.username) > 0) {
        strcpy(row.username, statement->row_to_update.username);
      }
      if (strlen(statement->row_to_update.email) > 0) {
        strcpy(row.email, statement->row_to_update.email);
      }
      serialize_row(&row, leaf_node_value(node, cursor->cell_num));
      updated_count++;
    }

    cursor_advance(cursor);
  }

  free(cursor);
  printf("%d row(s) updated.\n", updated_count);
}

void execute_statement(Statement* statement, Database* db) {
  if (statement->type == STATEMENT_CREATE_TABLE) {
    execute_create_table(statement, db);
    return;
  }

  Table table;
  if (!db_lookup_table(db, statement->table_name, &table)) {
    printf("Error: no such table '%s'.\n", statement->table_name);
    return;
  }

  switch (statement->type) {
    case STATEMENT_INSERT: execute_insert(statement, &table); break;
    case STATEMENT_SELECT: execute_select(statement, &table); break;
    case STATEMENT_DELETE: execute_delete(statement, &table); break;
    case STATEMENT_UPDATE: execute_update(statement, &table); break;
    default: break;
  }
}

// Main
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Must supply a database filename.\n");
    exit(EXIT_FAILURE);
  }

  char* filename = argv[1];
  Database* db = db_open(filename);
  InputBuffer* input_buffer = new_input_buffer();

  while (true) {
    print_prompt();

    if (!read_input(input_buffer)) {
      printf("\n");
      break;
    }

    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, db)) {
        case META_COMMAND_SUCCESS: continue;
        case META_COMMAND_UNRECOGNIZED_COMMAND:
          printf("Unrecognized command '%s'\n", input_buffer->buffer);
          continue;
        case META_COMMAND_BTREE: continue;
      }
    }

    Statement statement;
    switch (prepare_statement(input_buffer, &statement)) {
      case PREPARE_SUCCESS: break;
      case PREPARE_SYNTAX_ERROR: printf("Syntax error. Could not parse statement.\n"); continue;
      case PREPARE_NEGATIVE_ID: printf("ID must be positive.\n"); continue;
      case PREPARE_STRING_TOO_LONG: printf("String is too long.\n"); continue;
      case PREPARE_UNRECOGNIZED_STATEMENT: printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer); continue;
    }

    execute_statement(&statement, db);
  }

  close_input_buffer(input_buffer);
  db_close(db);
  exit(EXIT_SUCCESS);
}

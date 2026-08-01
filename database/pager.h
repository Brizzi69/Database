#ifndef PAGER_H
#define PAGER_H

#include <stdint.h>

#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 1000

// Reads and writes fixed-size pages to a database file, caching pages
// that have already been loaded from disk.
typedef struct {
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_pages;
    void *pages[TABLE_MAX_PAGES];
} Pager;

Pager *pager_open(const char *filename);

// Returns the in-memory page, reading it from disk on first access.
void *get_page(Pager *pager, uint32_t page_num);

// Grows the file by one page and returns its page number.
uint32_t allocate_page(Pager *pager);

void pager_flush(Pager *pager, uint32_t page_num, uint32_t size);

#endif

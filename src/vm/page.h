#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdio.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"

#define STACK_SIZE (8 * (1 << 20))

enum suppl_pte_type
{
  SWAP = 001,
  FILE = 002,
  MMF  = 004
};

union suppl_pte_data{
    struct {
        struct file * file;
        off_t ofs;
        uint32_t read_bytes;
        uint32_t zero_bytes;
        bool writable;
    } file_page;

    struct {
        struct file *file;
        off_t ofs;
        uint32_t read_bytes;
    } mmf_page;
};

struct suppl_pte{
    void* usr_vadr;
    enum suppl_pte_type type;
    union suppl_pte_data data;
    bool is_loaded;

    /* for swapping */
    size_t swap_slot_idx;
    bool swap_writable;

    struct hash_elem elem;
};

void vm_page_init(void);
unsigned suppl_pt_hash(const struct hash_elem*, void* UNUSED);
bool suppl_pt_less(const struct hash_elem*, const struct hash_elem*, void* UNUSED);
bool insert_suppl_pte(struct hash*, struct suppl_pte*);
bool suppl_pt_insert_file( struct file*, off_t, uint8_t*, uint32_t, uint32_t, bool);
bool suppl_pt_insert_mmf (struct file *, off_t, uint8_t *, uint32_t);
struct suppl_pte* get_suppl_pte(struct hash*, void*);
void write_page_back_to_file_wo_lock(struct suppl_pte*);
void free_suppl_pt(struct hash*);
bool load_page(struct suppl_pte*);
void grow_stack(void*);

#endif
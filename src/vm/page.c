#include "string.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "filesys/file.h"

static bool load_page_file (struct suppl_pte*);
static bool load_page_swap (struct suppl_pte*);
static bool load_page_mmf (struct suppl_pte*);
static void free_suppl_pte (struct hash_elem*, void* UNUSED);

void
vm_page_init(void){
    return;
}

unsigned 
suppl_pt_hash(const struct hash_elem* he, void* aux UNUSED){
    struct supple_pte* vspte;
    vspte = hash_entry(he, struct suppl_pte,elem);
    return hash_bytes(&(vspte->usr_vadr),size(vspte->usr_vadr));
}

bool 
suppl_pt_less(const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED){
    const struct suppl_pte* vspte_a;
    const struct suppl_pte* vspte_b;
 
    vspte_a = hash_entry(a, struct suppl_pte, elem);
    vspte_b = hash_entry(b, struct suppl_pte, elem);

    return (vspte_a->usr_vadr - vspte_b->usr_vadr) < 0;
}

bool 
insert_suppl_pte(struct hash*, struct suppl_pte*);

bool 
suppl_pt_insert_file( struct file*, off_t, uint8_t*, uint32_t, uint32_t, bool);

bool 
suppl_pt_insert_mmf (struct file *, off_t, uint8_t *, uint32_t);




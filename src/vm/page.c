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

/* placeholder initalization */
void
vm_page_init(void){
    return;
}

/* hash function for hash table suppl page table*/
unsigned 
suppl_pt_hash(const struct hash_elem* he, void* aux UNUSED){
    struct suppl_pte* vspte;
    vspte = hash_entry(he, struct suppl_pte,elem);
    return hash_bytes(&(vspte->usr_vadr),sizeof(vspte->usr_vadr));
}

/* less function for hash table  */
bool 
suppl_pt_less(const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED){
    const struct suppl_pte* vspte_a;
    const struct suppl_pte* vspte_b;
 
    vspte_a = hash_entry(a, struct suppl_pte, elem);
    vspte_b = hash_entry(b, struct suppl_pte, elem);

    return (vspte_a->usr_vadr - vspte_b->usr_vadr) < 0;
}

/* insert a spte to spt via hash table functions */
bool 
insert_suppl_pte (struct hash *spt, struct suppl_pte *spte)
{
  struct hash_elem *result;

  if (spte == NULL)
    return false;
  
  result = hash_insert (spt, &spte->elem);
  if (result != NULL)
    return false;
  
  return true;
}

/* insert an spte of tyoe file to suppl page table */
bool
suppl_pt_insert_file (struct file *file, off_t ofs, uint8_t *upage, 
		      uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct suppl_pte *spte; 
  struct hash_elem *result;
  struct thread *cur = thread_current ();

  spte = calloc (1, sizeof *spte);
  
  if (spte == NULL)
    return false;
  
  spte->usr_vadr = upage;
  spte->type = FILE;
  spte->data.file_page.file = file;
  spte->data.file_page.ofs = ofs;
  spte->data.file_page.read_bytes = read_bytes;
  spte->data.file_page.zero_bytes = zero_bytes;
  spte->data.file_page.writable = writable;
  spte->is_loaded = false;
      
  result = hash_insert (&cur->suppl_page_table, &spte->elem);
  if (result != NULL)
    return false;

  return true;
}


/* inset an spte of type mmf to suppl page table */
bool
suppl_pt_insert_mmf (struct file *file, off_t ofs, uint8_t *upage, 
		      uint32_t read_bytes)
{
  struct suppl_pte *spte; 
  struct hash_elem *result;
  struct thread *cur = thread_current ();

  spte = calloc (1, sizeof *spte);
      
  if (spte == NULL)
    return false;
  
  spte->usr_vadr = upage;
  spte->type = MMF;
  spte->data.mmf_page.file = file;
  spte->data.mmf_page.ofs = ofs;
  spte->data.mmf_page.read_bytes = read_bytes;
  spte->is_loaded = false;
      
  result = hash_insert (&cur->suppl_page_table, &spte->elem);
  if (result != NULL)
    return false;

  return true;
}

/* get an entry in suppl page table accoring to uvaddress which is the key */
struct suppl_pte *
get_suppl_pte (struct hash *ht, void *uvaddr)
{
  struct suppl_pte spte;
  struct hash_elem *e;

  spte.usr_vadr = uvaddr;
  e = hash_find (ht, &spte.elem);
  return e != NULL ? hash_entry (e, struct suppl_pte, elem) : NULL;
}

/* writing an mmf back to file sys, this is required when page is dirty */
void write_page_back_to_file_wo_lock (struct suppl_pte *spte)
{
  if (spte->type == MMF)
    {
      // lock_acquire(&file_lock);
      file_seek (spte->data.mmf_page.file, spte->data.mmf_page.ofs);
      file_write (spte->data.mmf_page.file, 
                  spte->usr_vadr,
                  spte->data.mmf_page.read_bytes);
      // lock_release(&file_lock);
    }
}

/* free the entry represented by the hash emelent */
static void
free_suppl_pte (struct hash_elem *e, void *aux UNUSED)
{
  struct suppl_pte *spte;
  spte = hash_entry (e, struct suppl_pte, elem);
  if (spte->type & SWAP)
    vm_clear_swap_slot (spte->swap_slot_idx);

  free (spte);
}

/* free the entire hash table*/
void free_suppl_pt (struct hash *suppl_pt) 
{
  hash_destroy (suppl_pt, free_suppl_pte);
}


/* read file from suppl pt to a page of memory and add the page to address space */
static bool
load_page_file (struct suppl_pte *spte)
{
    struct thread *cur = thread_current ();
    struct frame* fm;

    file_seek (spte->data.file_page.file, spte->data.file_page.ofs);
    uint8_t *kpage = frame_allocate (PAL_USER);
    if (kpage == NULL)
        return false;
    
    off_t read_ret = file_read (spte->data.file_page.file, kpage,
            spte->data.file_page.read_bytes);
    if (read_ret != (int) spte->data.file_page.read_bytes)
    {
        frame_free (kpage);
        return false; 
    }
    memset (kpage + spte->data.file_page.read_bytes, 0,
        spte->data.file_page.zero_bytes);
    
    if (!pagedir_set_page (cur->pagedir, spte->usr_vadr, kpage,
                spte->data.file_page.writable))
    {
        frame_free (kpage);
        return false; 
    }

    fm = get_frame(kpage);
    fm->evictable = true;

    spte->is_loaded = true;
    
    return true;
}


/* Load a mmf page defined in struct suppl_pte to a page in 
   memory and adding the page to address space */
static bool
load_page_mmf (struct suppl_pte *spte)
{
    struct thread *cur = thread_current ();
    struct frame* fm;

    file_seek (spte->data.mmf_page.file, spte->data.mmf_page.ofs);
    uint8_t *kpage = frame_allocate (PAL_USER);
    if (kpage == NULL)
        return false;

    off_t read_ret = file_read (spte->data.mmf_page.file, kpage,
            spte->data.mmf_page.read_bytes);

    if (read_ret != (int) spte->data.mmf_page.read_bytes)
    {
        frame_free (kpage);
        return false; 
    }
    memset (kpage + spte->data.mmf_page.read_bytes, 0,
        PGSIZE - spte->data.mmf_page.read_bytes);

    if (!pagedir_set_page (cur->pagedir, spte->usr_vadr, kpage, true)) 
    {
        frame_free (kpage);
        return false; 
    }

    fm = get_frame(kpage);
    fm->evictable = true;

    spte->is_loaded = true;
    if (spte->type & SWAP)
        spte->type = MMF;

    return true;
}

/* load a page from the swap defined in the suppl_pte into memory
   After swap in, remove the corresponding entry in suppl page table */
static bool
load_page_swap (struct suppl_pte *spte)
{
    struct frame* fm;

    uint8_t *kpage = frame_allocate (PAL_USER);

    if (kpage == NULL)
        return false;

    if (!pagedir_set_page (thread_current ()->pagedir, spte->usr_vadr, kpage, 
                spte->swap_writable))
    {
        frame_free (kpage);
        return false;
    }

    vm_swap_in (spte->swap_slot_idx, spte->usr_vadr);
    fm = get_frame(kpage);
    fm->evictable = true;
    
    if (spte->type == SWAP)
    {
        hash_delete (&thread_current ()->suppl_page_table, &spte->elem);
    }
    if (spte->type == (FILE | SWAP))
    {
        spte->type = FILE;
        spte->is_loaded = true;
    }

    return true;
}

/* handler function, load page data to page */
bool
load_page (struct suppl_pte *spte)
{
    bool success = false;
    // lock_acquire(&file_lock);
    switch (spte->type)
    {
        case FILE:
            success = load_page_file (spte);
            break;
        case MMF:
        case MMF | SWAP:
            success = load_page_mmf (spte);
            break;
        case FILE | SWAP:
        case SWAP:
            success = load_page_swap (spte);
            break;
        default:
            break;
    }
    // lock_release(&file_lock);
    return success;
}

/* map uvadress to a newly allocated page which grows the stack 
   add the page to the process's address space. */ 
void 
grow_stack (void *uvaddr)
{
    void *spage;
    struct thread *t = thread_current ();
    struct frame* fm;
    
    spage = frame_allocate (PAL_USER | PAL_ZERO);
    if (spage == NULL) return;
    else {
        if (!pagedir_set_page (t->pagedir, pg_round_down (uvaddr), spage, true))
          frame_free (spage); 
    }
    fm = get_frame(spage);
    fm->evictable = true;
}



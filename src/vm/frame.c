#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

#include "vm/frame.h"

static struct lock frame_lock;
static struct lock eviction_lock;
static struct frame* frame_to_evict();
static bool save_evicted_frame (struct frame* ev_f);
static void remove_frame_entry(void*);
static bool append_frame(void*);

/* initialize the frame table and locks*/
void
frame_init ()
{
  list_init (&frames);
  lock_init (&frame_lock);
  lock_init (&eviction_lock);
}

/* allocate a page from USER_POOL, and add an entry to frame table 
   if palloc not sucess, eviction of frame is needed. if allocation successed,
   append to the frame list */
void* 
frame_allocate(enum palloc_flags flags){
    struct frame* fm;
    void* f = NULL;

    if (flags & PAL_USER)
    {
      if (flags & PAL_ZERO)
        f = palloc_get_page (PAL_USER | PAL_ZERO);
      else
        f = palloc_get_page (PAL_USER);
    }
    /*if allocation successed, append to the frame list*/
    if (f){
        append_frame(f);
    }
    else{
        f = evict_frame ();
        if (f == NULL) PANIC ("Evicting frame failed");
    } 
    fm = get_frame(f);
    fm->evictable = false;
    return f;
}

/* remove frame table entry then free frame physically. */
void 
frame_free (void* f){
    remove_frame_entry(f);
    palloc_free_page(f);
}

/* mapping pte attributes to frame table */
void 
frame_set_usr (void* fm, uint32_t* pte, void* uvadr){
    struct frame* f = get_frame(fm);
    if (f!=NULL){
        f->pte = pte;
        f->user_vadr = uvadr;
    }
}

/* frame eviction, select frame to evict and save the content to
   swap, then allocate the frame to current thread, frame_adr don't
   need to be changed since physical address doesn't change */
void*
evict_frame (void){
    bool result;
    struct frame *ev_f;

    lock_acquire (&eviction_lock);
    
    ev_f = frame_to_evict ();
    if (ev_f == NULL) PANIC ("No frame to evict.");
    result = save_evicted_frame (ev_f);
    if (!result) PANIC ("can't save evicted frame");
    
    ev_f->tid = thread_current()->tid;
    ev_f->pte = NULL;
    ev_f->user_vadr = NULL;
    
    lock_release(&eviction_lock);
    return ev_f->frame_adr;
}

/* traverse the frame table to find the frame */
struct frame*
get_frame(void* f){
    struct frame *fm;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    e = list_head(&frames);
    e = list_next(e);
    while(e != list_tail(&frames)){
        fm = list_entry(e,struct frame, elem);
        if (fm->frame_adr == f){
            break;
        }
        e = list_next(e);
        fm = NULL;
    }
    lock_release(&frame_lock);
    return fm;
}

/* select a frame to evict. iterate the frame list to find the first 
   frame that is not recently accessed while resetting the accessed indicator
   if nothing is found on the first round then it is similar to the "clock" or
   "second chance" algo*/
static struct frame*
frame_to_evict(){
    struct frame *ev_f;
    struct thread *t;
    struct list_elem *e;

    struct frame *class0 = NULL;
    int round_count = 1;
    bool found = false;
    while(!found){
        e = list_head(&frames);
        e = list_next(e);
        while(e!=list_tail(&frames)){
            ev_f = list_entry(e, struct frame, elem);
            t = get_thread_by_tid(ev_f->tid);
            bool accessed = pagedir_is_accessed(t->pagedir, ev_f->user_vadr);
            if (!accessed && ev_f->evictable){
                class0 = ev_f;
                list_remove(e);
                list_push_back(&frames,e);
            }
            else{
                pagedir_set_accessed (t->pagedir, ev_f->user_vadr, false);
            }
            e = list_next(e);
        }
        if (class0 != NULL)
            found = true;
        else if (round_count++ == 2)
            found = true;
    }
    return class0;

}

/* there may be a problem since swap_slot_idx is not initalized for mmf */
static bool
save_evicted_frame (struct frame* ev_f){
    struct thread *t;
    struct suppl_pte *spte;
    t = get_thread_by_tid(ev_f->tid);
    spte = get_suppl_pte (&t->suppl_page_table, ev_f->user_vadr);
    if (spte == NULL)
    {
        spte = calloc(1, sizeof *spte);
        spte->usr_vadr = ev_f->user_vadr;
        spte->type = SWAP;
        if (!insert_suppl_pte (&t->suppl_page_table, spte))
            return false;
    }
    size_t swap_slot_idx;
    if (pagedir_is_dirty (t->pagedir, spte->usr_vadr)&& (spte->type == MMF))
    {
        write_page_back_to_file_wo_lock (spte);
    }
    else if (pagedir_is_dirty (t->pagedir, spte->usr_vadr)|| (spte->type != FILE))
    {
        swap_slot_idx = vm_swap_out (spte->usr_vadr);
        if (swap_slot_idx == SWAP_ERROR)
            return false;

        spte->type = (spte->type | SWAP);
    }

    memset (ev_f->frame_adr, 0, PGSIZE);

    spte->swap_slot_idx = swap_slot_idx;
    spte->swap_writable = *(ev_f->pte) & PTE_W;

    spte->is_loaded = false;

    pagedir_clear_page (t->pagedir, spte->usr_vadr);

    return true;
}

/* iterate frame table to evict entry and free memory space */
static void
remove_frame_entry(void* f){
    struct frame *fm;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    e = list_head(&frames);
    e = list_next(e);
    while(e != list_tail(&frames)){
        fm = list_entry(e,struct frame, elem);
        if (fm->frame_adr == f){
            list_remove(e);
            free(fm);
            break;
        }
        e = list_next(e);
    }
    lock_release(&frame_lock);
}

/* appending an entry to frame table. operations on frame table need sync*/
static bool
append_frame(void* f){
    struct frame* fm;
    fm = calloc (1, sizeof(*fm));
    if (fm == NULL) return false;
    fm->tid = thread_current()->tid;
    fm->frame_adr = f;
    lock_acquire (&frame_lock);
    list_push_back (&frames, &fm->elem);
    lock_release (&frame_lock);
    return true;
}
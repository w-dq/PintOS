#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct lock frame_lock;
static struct lock eviction_lock;

void
frame_init ()
{
  list_init (&frames);
  lock_init (&frame_lock);
  lock_init (&eviction_lock);
}

/* allocate a page from USER_POOL, and add an entry to frame table */
void* 
frame_allocate(enum palloc_flags flags){
    void* f = NULL;
    if (flags == PAL_USER){      ////////
        f = palloc_get_page (PAL_USER);
    }
    /*if allocation successed, append to the frame list*/
    if (f){
        append_frame(f);
    }
    else{
        void* evict_f = evict_frame ();
        if (evict_f == NULL) PANIC ("Evicting frame failed");
    } 
    return f;
}

void 
frame_free (void* f){
    /* remove frame table entry */
    remove_frame_entry(f);
    /* free frame physically */
    palloc_free_page(f);
}

void 
frame_set_usr (void* fm, uint32_t* pte, void* uvadr){
    struct frame* f = get_frame(fm);
    if (f!=NULL){
        f->pte = pte;
        f->user_vadr = uvadr;
    }
}

/* evict a frame and save its content for later swap in */
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
struct frame *
get_frame(void* f){
    struct frame *fm;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    e = list_head(&frames);
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

/* select a frame to evict */
struct frame*
frame_to_evict(){
    struct frame *ev_f;
    struct thread *t;
    struct list_elem *e;

    struct frame *class0 = NULL;
    int round_count = 1;
    bool found = false;

}
save_evicted_frame (ev_f){}

void
remove_frame_entry(void* f){
    struct frame *fm;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    e = list_head(&frames);
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

bool
append_frame(void* f){
    struct frame* fm;
    fm = calloc (1, size(*fm));
    if (fm == NULL) return false;
    fm->tid = thread_current()->tid;
    fm->frame_adr = f;
    lock_acquire (&frame_lock);
    list_push_back (&frames, &fm->elem);
    lock_release (&frame_lock);
    return true;
}
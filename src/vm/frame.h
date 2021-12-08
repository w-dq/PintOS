#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"

struct frame{
    void* frame_adr;            /* physical address */
    void* user_vadr;            /* user virtual address */
    uint32_t* pte;              /* page table entry pointer */
    tid_t tid;                  /* thread id */
    struct list_elem elem;      /* list elem to indicate in list */
};

struct list frames;

void frame_init (void);
void* frame_allocate(enum palloc_flags flags);
void frame_free (void *);

void frame_set_usr (void*, uint32_t *, void *);

/* evict a frame to be freed and write the content to swap slot or file*/
void* evict_frame (void);





#endif /* vm/frame.h */
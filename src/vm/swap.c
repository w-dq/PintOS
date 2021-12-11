#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

#include "vm/swap.h"

struct block *swap_device;

struct lock swap_lock;

static struct bitmap *swap_map;

static size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;
static size_t swap_size_in_page (void);

/* init swap device and swap bitmap and set all bits to true*/
void
vm_swap_init ()
{
  lock_init(&swap_lock);

  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("no swap device found, can't initialize swap");
  
  lock_acquire(&swap_lock);
  swap_map = bitmap_create (swap_size_in_page ());
  
  if (swap_map == NULL){
    lock_release(&swap_lock);
    PANIC ("swap bitmap creation failed");
  }

  bitmap_set_all (swap_map, true);
  lock_release(&swap_lock);
}

/* find a swap slot to dump and return error if not found 
   block write in sectors, so the start of an index would be 
   swap_idx * SECTORS_PER_PAGE and corresbonding address would
   be uva + counter*BLOCK_SECTOR_SIZE */
size_t vm_swap_out (const void *uva)
{
  lock_acquire(&swap_lock);
  size_t swap_idx = bitmap_scan_and_flip (swap_map, 0, 1, true);
  lock_release(&swap_lock);
  if (swap_idx == BITMAP_ERROR){
    
    return SWAP_ERROR;
  }

  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
    {
      block_write (swap_device, swap_idx * SECTORS_PER_PAGE + counter, 
		   uva + counter * BLOCK_SECTOR_SIZE);
      counter++;
    }
  return swap_idx;
}

/* read form swap device similarly to writing */
void
vm_swap_in (size_t swap_idx, void *uva)
{
  
  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
    {
      block_read (swap_device, swap_idx * SECTORS_PER_PAGE + counter,
		  uva + counter * BLOCK_SECTOR_SIZE);
      counter++;
    }
  lock_acquire(&swap_lock);
  bitmap_flip (swap_map, swap_idx);
  lock_release(&swap_lock);
}

/* used in page.c when freeing (spte->type & SWAP)
   SWAP should have been set to false so flipping would do the trick
   however, could use set to be safe */
void vm_clear_swap_slot (size_t swap_idx)
{
  lock_acquire(&swap_lock);
  bitmap_flip (swap_map, swap_idx);  
  lock_release(&swap_lock);
}


/* block devices are counted in blocks thus if we want
   to count in pages we devide SECTORS_PER_PAGE */
static size_t
swap_size_in_page ()
{
  return block_size (swap_device) / SECTORS_PER_PAGE;
}


#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

#include "vm/swap.h"

struct block *swap_device;

static struct bitmap *swap_map;

static size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;
static size_t swap_size_in_page (void);

void
vm_swap_init ()
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("no swap device found, can't initialize swap");

  swap_map = bitmap_create (swap_size_in_page ());
  if (swap_map == NULL)
    PANIC ("swap bitmap creation failed");

  bitmap_set_all (swap_map, true);
}

size_t vm_swap_out (const void *uva)
{

  size_t swap_idx = bitmap_scan_and_flip (swap_map, 0, 1, true);
    
  if (swap_idx == BITMAP_ERROR)
    return SWAP_ERROR;

  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
    {
      block_write (swap_device, swap_idx * SECTORS_PER_PAGE + counter, 
		   uva + counter * BLOCK_SECTOR_SIZE);
      counter++;
    }
  return swap_idx;
}

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
  bitmap_flip (swap_map, swap_idx);
}

void vm_clear_swap_slot (size_t swap_idx)
{
  bitmap_flip (swap_map, swap_idx);  
}

static size_t
swap_size_in_page ()
{
  return block_size (swap_device) / SECTORS_PER_PAGE;
}


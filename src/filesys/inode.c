#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos < inode->data.length) {
    if (pos < LENGTH_PASS_DIRECT) {
      return inode->data.direct[pos / BLOCK_SECTOR_SIZE];
    } else if (pos < LENGTH_PASS_INDIRECT){
      uint32_t idx1,idx2;
      uint32_t indirect_table[128];
      idx1 = (pos - LENGTH_PASS_DIRECT) / (BLOCK_SECTOR_SIZE * 128);
      idx2 = (pos - LENGTH_PASS_DIRECT - idx1 * 128 * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;
      block_read(fs_device,inode->data.indirect[idx1],indirect_table);
      return indirect_table[idx2];
    } else {
      uint32_t idx1,idx2;
      uint32_t doubly_indirect[128];
      idx1 = (pos - LENGTH_PASS_INDIRECT) / (BLOCK_SECTOR_SIZE * 128);
      idx2 = (pos - LENGTH_PASS_INDIRECT - idx1 * 128 * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;

      block_read(fs_device,inode->data.doubly_indirect,doubly_indirect);
      block_read(fs_device,doubly_indirect[idx1],doubly_indirect);
      return doubly_indirect[idx2];
    }
  } else {
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_file)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  { 
    disk_inode->magic = INODE_MAGIC;
    /* is_file indication */
    disk_inode->is_file = is_file;
    /*initalize as zero then extend to the target length */
    disk_inode->length = 0;
    disk_inode->direct_ptr = 0;
    disk_inode->indirect_ptr1 = 0;
    disk_inode->indirect_ptr2 = 0;
    disk_inode->doubly_ptr2 = 0;
    disk_inode->doubly_ptr3 = 0;
    if(inode_extend(&disk_inode, length) == length){
      block_write(fs_device, sector, disk_inode);
      success = true;
    }
    free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
  {
    inode = list_entry (e, struct inode, elem);
    if (inode->sector == sector) 
      {
        inode_reopen (inode);
        return inode; 
      }
  }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  
  block_read (fs_device, inode->sector, &inode->data);
  lock_init(&inode->extend_lock);
  inode->length = inode->data.length;
  inode->length_for_read = inode->data.length;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_inode_disk(&inode->data);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_have_read = 0;
  off_t bytes_to_read = inode->length_for_read;
  struct cache_block* block;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = bytes_to_read - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      // get data into cache
      block = get_cache_block(sector_idx, 0);
      // read from cache
      memcpy (buffer + bytes_have_read, (uint8_t*)&(block->block_data) + sector_ofs, chunk_size);
      block->reference_bit = 1;
      block->occupied_count--;
      
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_have_read += chunk_size;
    }
  // free (bounce);

  return bytes_have_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  
  struct cache_block* block;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Write full sector directly to disk. */
      //     block_write (fs_device, sector_idx, buffer + bytes_written);
      //   }
      // else 
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left) 
      //       block_read (fs_device, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, BLOCK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     block_write (fs_device, sector_idx, bounce);
      //   }

      block = get_cache_block(sector_idx, 1);
      // read from cache
      memcpy ((uint8_t*)&(block->block_data) + sector_ofs, buffer+bytes_written, chunk_size);
      block->reference_bit = 1;
      block->occupied_count--;

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  inode->length_for_read = inode->length;

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* extend the inode to target length with zeros,*/
off_t inode_extend(struct inode_disk *inode_d, off_t target_length){
    /* make sure doesn't excced limit */
    ASSERT(target_length < 8*1024*1024);
    bool success;

    static char zeros[BLOCK_SECTOR_SIZE];
    static uint32_t level1[128];
    static uint32_t level2[128];
    size_t new_sectors = bytes_to_sectors (target_length) - bytes_to_sectors(inode_d->length);

    if (new_sectors == 0) return target_length;

    /* initialize level1 and level2 if half-way done*/
    if (inode_d->doubly_ptr3 != 0) {
      /* doubl-indirect already in-use */
      block_read(fs_device, inode_d->doubly_indirect,level1);
      block_read(fs_device, level1[inode_d->doubly_ptr3],level2);
    } else if (inode_d->indirect_ptr2 != 0) {
      /* indirect already in-use */
      block_read(fs_device, inode_d->indirect[inode_d->indirect_ptr1],level1);
    }

    /* bear in mind that ptr always points to to next sector to right */
    while(new_sectors>0){
      if (inode_d->direct_ptr < DIRECT_LENGTH){
        /* direct table */
        success = free_map_allocate(1, &inode_d->direct[inode_d->direct_ptr]);
        if(!success) return 0;
        block_write(fs_device, inode_d->direct[inode_d->direct_ptr], zeros);
        inode_d->direct_ptr++;
        new_sectors--;
      } else if (inode_d->indirect_ptr1 < INDIRECT_LENGTH) {
        /* need to put in indirect table*/
        if(inode_d->indirect_ptr2 < 128){
          success = free_map_allocate(1, &level1[inode_d->indirect_ptr2]);
          if(!success) return 0;
          block_write(fs_device, level1[inode_d->indirect_ptr2], zeros);
          inode_d->indirect_ptr2++;
          new_sectors--;
        }
        if(inode_d->indirect_ptr2 == 128){
          /* level1 full, move to next indrect table */
          success = free_map_allocate(1, &inode_d->indirect[inode_d->indirect_ptr1]);
          if(!success) return 0;
          block_write(fs_device, inode_d->indirect[inode_d->indirect_ptr1], level1);
          inode_d->indirect_ptr1++;
          inode_d->indirect_ptr2 = 0;
        }
      } else if (inode_d->doubly_ptr2 < 128){
        if (inode_d->doubly_ptr3 < 128){
          success = free_map_allocate(1, &level2[inode_d->doubly_ptr3]);
          if(!success) return 0;
          block_write(fs_device, level2[inode_d->doubly_ptr3], zeros);
          inode_d->doubly_ptr3++;
          new_sectors--;
        }
        if(inode_d->doubly_ptr3 == 128){
          /* level2 full, move to next double-indrect table */
          success = free_map_allocate(1, &level1[inode_d->doubly_ptr2]);
          if(!success) return 0;
          block_write(fs_device, level1[inode_d->doubly_ptr2], level2);
          inode_d->doubly_ptr2++;
          inode_d->doubly_ptr3 = 0;
        } 
      } else {
          return false;
      }

    /* write back unfinished level one and level two */
    if (inode_d->doubly_ptr3 != 0) {
      /* doubl-indirect already in-use */
      block_write(fs_device,inode_d->doubly_indirect,level1);
      block_write(fs_device,level1[inode_d->doubly_ptr3],level2);
    } else if (inode_d->indirect_ptr2 != 0) {
      /* indirect already in-use */
      block_write(fs_device, inode_d->indirect[inode_d->indirect_ptr1],level1);
    }

    /* WARN: if failed, should extend at all? */
    inode_d->length = target_length;
    return target_length;
}

int sec_to_idx(size_t sec_num, int idx_type){
  switch (idx_type){
    case 0: /* direct */
      return sec_num;
    case 1: /* indirect level 1 */
      return (sec_num - DIRECT_LENGTH) / 128;
    case 2: /* indirect level 2 */
      uint32_t idx1 = (sec_num - DIRECT_LENGTH) / 128;
      return sec_num - DIRECT_LENGTH - idx1 * 128;
    case 3: /* doubly level 2 */
      return (sec_num - INDIRECT_LENGTH) / 128;
    case 4: /* doubly level 3 */
      uint32_t idx1 = (sec_num - INDIRECT_LENGTH) / 128;
      return sec_num - INDIRECT_LENGTH - idx1 * 128;
  }
}

void free_inode_disk(struct inode_disk* inode_d){
  static uint32_t level1[128];
  static uint32_t level2[128];

  size_t sec_to_free = bytes_to_sectors(inode_d->length);
  size_t sec_freed = 0;
  while (sec_freed < sec_to_free){
    if (sec_freed < DIRECT_LENGTH){
      free_map_release(inode_d->direct[sec_to_idx(sec_to_free,0)],1);
      sec_freed++;
    } else if (sec_freed < INDIRECT_LENGTH){
      uint32_t idx1 = sec_to_idx(sec_to_free,1);
      uint32_t idx2 = sec_to_idx(sec_to_free,2);
      block_read(fs_device, inode_d->indirect[idx], level1);
      for(uint8_t i=0; i < idx2; i++){
        free_map_release(level1[i],1);
        sec_freed++;
      }
      free_map_release(inode_d->indirect[idx],1);
    } else {
      uint32_t idx3 = sec_to_idx(sec_to_free,3);
      block_read(fs_device, inode_d->doubly_indirect, level1);

      uint32_t idx4 = sec_to_idx(sec_to_free,4);
      for (uint8_t j = 0; j < idx3; j++)
      {
        block_read(fs_device, level1[j], level2);
        for(uint8_t i=0; i < 128; i++){
          free_map_release(level2[i],1);
          sec_freed++;
        }
        free_map_release(level1[j], 1);
      }

      block_read(fs_device, level1[idx3], level2);
      for(uint8_t i=0; i < idx4; i++){
        free_map_release(level2[i],1);
        sec_freed++;
      }
      free_map_release(level1[idx3], 1);

      free_map_release(inode_d->doubly_indirect, 1);
    }
    
  }
  
}
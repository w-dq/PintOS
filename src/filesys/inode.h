#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "list.h"
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

#define DIRECT_LENGTH           (100)
#define LENGTH_PASS_DIRECT      (DIRECT_LENGTH*BLOCK_SECTOR_SIZE)

#define INDIRECT_LENGTH         (10)
#define LENGTH_PASS_INDIRECT    (LENGTH_PASS_DIRECT + 128*BLOCK_SECTOR_SIZE*DIRECT_LENGTH)
#define LENGTH_UNUSED           (128 - 9 - DIRECT_LENGTH - INDIRECT_LENGTH)
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. 
   8MB at least
   direct:  512 B = 0.5 KB
   indirect: 128*0.5KN=B = 64KB
   double indirect:  128*64 KB = 8MB  */
struct inode_disk
{
    off_t length;                       /* 1: File size in bytes. */
    unsigned magic;                     /* 2: Magic number. */
    uint32_t is_file;                   /* 3: 1 if file, 0 if dir */
    uint32_t direct_ptr;                /* 4: index of the direct list */
    uint32_t indirect_ptr1;             /* 5: index of the level 1 indrect table */
    uint32_t indirect_ptr2;             /* 6: index of the level 2 indrect table */
    /* doubly only need two level since only one doubly_indirect */
    uint32_t doubly_ptr2;               /* 7: index of the level 2 doubly table */
    uint32_t doubly_ptr3;               /* 8: index of the level 3 doubly table */

    /* direct */
    block_sector_t direct[DIRECT_LENGTH];
    /* indirect */
    block_sector_t indirect[INDIRECT_LENGTH];
    /* doubly_indirect */
    block_sector_t doubly_indirect;     /* 9 */
    /* indirect is not needed for sake of simplicity */

    uint32_t unused[LENGTH_UNUSED];               /* Not used. */
};

/* In-memory inode. */
struct inode 
{
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    //added
    struct lock extend_lock;
    off_t length_for_read;              /* calculate the file size to read in bytes */
    off_t length;                       /* the file size in bytes */
};

void inode_init (void);
bool inode_create (block_sector_t, off_t, uint32_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
off_t inode_extend(struct inode_disk *, off_t );

#endif /* filesys/inode.h */

#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block *fs_device;

void filesys_init (bool);
void filesys_done (void);
bool filesys_create (const char *, off_t);
struct file *filesys_open (const char *);
bool filesys_remove (const char *);
bool filesys_chdir(const char*);
bool filesys_mkdir (const char*);
bool filesys_create_dir(const char*);

#endif /* filesys/filesys.h */

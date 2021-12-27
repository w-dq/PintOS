#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
static bool parse_dir(char*, struct dir**, char base_name[NAME_MAX + 1]);
static struct inode* filepath_get_inode(const char*);
static bool parse_fail(struct dir**, struct dir*, char base_name[NAME_MAX+1]);
static int filename_get_this_point_next(char this[NAME_MAX + 1], char**);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  // cache_init();
  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  // scan_cache_write_back(true);
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector;
  struct dir *dir;
  char base_name[NAME_MAX + 1];

  bool success = (parse_dir(name,&dir,base_name)
                  && free_map_allocate (1, &inode_sector));
  if (success){
    struct inode *inode;
    inode = file_create (inode_sector, initial_size); 
    if (inode != NULL) {
      success = dir_add (dir, base_name, inode_sector);
      if (!success)
        inode_remove (inode);
      inode_close (inode);
    }
    else{
      success = false;
    }
  }
  dir_close (dir);

  return success;
}
// added
// need further check
bool
filesys_create_dir(const char* name){
  struct dir *dir;
  char base_name[NAME_MAX + 1];
  block_sector_t inode_sector;

  if (parse_dir(name, &dir, base_name)
      && free_map_allocate(1, &inode_sector)){
    struct inode* inode;
    inode = dir_create(inode_sector,dir->inode->sector); 
    if (inode){
      if (!dir_add(dir, base_name, inode_sector)){
        inode_remove (inode);
        inode_close (inode);
        dir_close (dir);
        return false;
      }
       
      dir_close (dir);
      return true;
    } else {
      dir_close (dir);
      return false;
    }
  } else {
    dir_close(dir);
    return false;
  }
}
/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}
// added
bool
filesys_chdir (const char* dir){
  struct dir *open_dir = dir_open(filepath_get_inode(dir));
  if (open_dir){
    dir_close(thread_current()->cur_dir);
    thread_current()->cur_dir = open_dir;
    return true;
  }
  else return false;
}
// added
bool 
filesys_mkdir (const char* dir){
  if (strcmp(dir, "")==0){
    return false;
  }
  else{
    return filesys_create_dir(dir);
  }
}
// added
static struct inode*
filepath_get_inode(const char* filepath){
  struct inode* ind;
  if (filepath[0] == '/' && filepath[strspn (filepath, "/")] == '\0'){
    return inode_open (ROOT_DIR_SECTOR);
  }
  else 
  {
    struct dir *dir;
    char base_name[NAME_MAX + 1];

    if(parse_dir(filepath, &dir, base_name)){
      dir_lookup(dir, base_name, &ind);
      dir_close(dir);
      return ind; 
    }
    else return NULL;
  }
}
// added
/* NEXT: the location of the pointer to the first char, 
          is updated to the next filename(exclude /) after the function 
   THIS[NAME_MAX+1]: hold the first filename from NEXT 
   this function only used in PARSE_DIR() 
   return 1 if success, return -1 if len(filename) > 14 */
static int
filename_get_this_point_next(char this[NAME_MAX + 1], char** next){
  char *ptr = *next;
  char *filename = this;
  while (*ptr == '/')
    ptr++;
  if (*ptr == '\0') {
    *next = ptr;
    *filename = '\0';
    return 1;
  }
  while (*ptr != '/' && *ptr != '\0'){
    if (filename < this + NAME_MAX)
        *filename++ = *ptr;
    else return -1;
    ptr++; 
  }
  *filename = '\0';
/* Advance source pointer. */
  while (*ptr == '/')
    ptr++;
  *next = ptr;
  return 1;
}

static bool
parse_fail(struct dir** dir, struct dir* dir_tmp, char base_name[NAME_MAX+1]){
  dir_close (dir_tmp);
  *dir = NULL;
  base_name[0] = '\0';
  return false;
}
// added
/* create and open DIR 
   parse DIR_STR recursively based on '/' and save last file into base_name */
static bool
parse_dir(char* dir_str, struct dir** dir, char base_name[NAME_MAX + 1]){
  // /home/pintos/group07/src/filesys/directory.c 
  // -> open dir /home/pintos/group07/src/filesys/directory.c  
  // base_name = 'directory.c\0'
  // /home/pintos/group07/src/filesys/directory/
  // -> open dir /home/pintos/group07/src/filesys/directory/ 
  // base_name = 'filesys\0'

  char cur_file[NAME_MAX+1];
  // char second[NAME_MAX+1];
  struct dir* dir_tmp;
  char* dir_str_tmp = dir_str;
  struct inode* ind;
  if ((dir_str[0] == '/')||thread_current()->cur_dir == NULL){
    dir_tmp = dir_open_root();
  }
  else{
    dir_tmp = dir_reopen(thread_current()->cur_dir);
  }

  if ((dir_tmp == NULL)
    ||(dir_tmp->inode->removed)){
    return parse_fail(dir,dir_tmp,base_name);
  }
  /* after this step, cur_file hold the foremost filename and dir_str_tmp point to the next */
  int success = filename_get_this_point_next(cur_file,&dir_str_tmp);
  
  // if it's the last one, we won't open the directory and just break
  while(success > 0){
    if (!dir_lookup(dir_tmp, cur_file, &ind)){
      return parse_fail(dir,dir_tmp,base_name);
    }
    dir_close(dir_tmp);

    dir_tmp = dir_open(ind);
    if ((dir_tmp == NULL)
      ||(!dir_tmp->inode->removed)){
      return parse_fail(dir,dir_tmp,base_name);
    }
    // if it is the last one, end the loop.
    if (*dir_str_tmp == '\0') break;

    success = filename_get_this_point_next(cur_file, &dir_str_tmp);
    
    // if LEN(CUR_FILE) > MAX_NAME, parse failed.
    if (success < 0){
      return parse_fail(dir,dir_tmp,base_name);  
    }
  }
  *dir = dir_tmp;
  strlcpy (base_name, cur_file, NAME_MAX + 1);
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

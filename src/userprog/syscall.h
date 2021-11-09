#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"
#include "lib/kernel/list.h"

struct file_node{
  int fd;                   /*file descriptor*/
  struct list_elem elem;    
  struct file* f;
  // int read_dir_cnt;
};

void exit_ret(int);

void syscall_init (void);

void sys_halt(struct intr_frame *f );
void sys_exit(struct intr_frame *f );
void sys_exec(struct intr_frame *f );
void sys_wait(struct intr_frame *f );
void sys_create(struct intr_frame *f );
void sys_remove(struct intr_frame *f );
void sys_open_file(struct intr_frame *f );
void sys_filesize(struct intr_frame *f );
void sys_read(struct intr_frame *f );
void sys_write(struct intr_frame *f );
void sys_seek(struct intr_frame *f );
void sys_tell(struct intr_frame *f );
void sys_close(struct intr_frame *f );

void sys_mmap(struct intr_frame *f );
void sys_munmap(struct intr_frame *f );

void sys_chdir(struct intr_frame *f );
void sys_mkdir(struct intr_frame *f );
void sys_readdir(struct intr_frame *f );
void sys_isdir(struct intr_frame *f );
void sys_inumber(struct intr_frame *f );


#endif /* userprog/syscall.h */

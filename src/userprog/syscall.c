#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


typedef void (*syscall_func)(struct intr_frame * UNUSED);
syscall_func syscalls[20];
static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  // register syscall functions for each syscall
  
  syscalls[SYS_HALT] = sys_halt;
  syscalls[SYS_EXIT] = sys_exit;
  syscalls[SYS_EXEC] = sys_exec;
  syscalls[SYS_WAIT] = sys_wait;
  syscalls[SYS_CREATE] = sys_create;
  syscalls[SYS_REMOVE] = sys_remove; 
  syscalls[SYS_OPEN] = sys_open;
  syscalls[SYS_FILESIZE] = sys_filesize;
  syscalls[SYS_READ] = sys_read;
  syscalls[SYS_WRITE] = sys_write;
  syscalls[SYS_SEEK] = sys_seek;
  syscalls[SYS_TELL] = sys_tell;
  syscalls[SYS_CLOSE] = sys_close;

  syscalls[SYS_MMAP] = sys_mmap;
  syscalls[SYS_MUNMAP] = sys_munmap;

  syscalls[SYS_CHDIR] = sys_chdir;
  syscalls[SYS_MKDIR] = sys_mkdir;
  syscalls[SYS_READDIR] = sys_readdir;
  syscalls[SYS_ISDIR] = sys_isdir;
  syscalls[SYS_INUMBER] = sys_inumber;
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* printf ("system call!\n");
   thread_exit ();*/
  
  // retrieve number and args 
  int number = *(int*)(f->esp);
  if ((number > SYS_INUMBER) || (number < 0)){
    thread_exit();   
  }
  // pass on to syscalls
  syscalls[number](f);
}

// implement syscall functions
void sys_halt(struct intr_frame *f){
  shutdown_power_off();
}
void sys_exit(struct intr_frame *f){
  thread_exit();
}
void sys_exec(struct intr_frame *f){

}
void sys_wait(struct intr_frame *f){}
void sys_create(struct intr_frame *f){}
void sys_remove(struct intr_frame *f){}
void sys_open(struct intr_frame *f){}
void sys_filesize(struct intr_frame *f){}
void sys_read(struct intr_frame *f){}

void sys_write(struct intr_frame *f){
}

void sys_seek(struct intr_frame *f){}
void sys_tell(struct intr_frame *f){}
void sys_close(struct intr_frame *f){}

void sys_mmap(struct intr_frame *f){
  printf("sys_mmap");
}
void sys_munmap(struct intr_frame *f){
  printf("sys_munmap");
}

void sys_chdir(struct intr_frame *f){
  printf("sys_chdir");
}
void sys_mkdir(struct intr_frame *f){
  printf("sys_mkdir");
}
void sys_readdir(struct intr_frame *f){
  printf("sys_readdir");
}
void sys_isdir(struct intr_frame *f){
  printf("sys_isdir");
}
void sys_inumber(struct intr_frame *f){
  printf("sys_inumber");
}

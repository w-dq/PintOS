#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"


typedef void (*syscall_func)(struct intr_frame * UNUSED);
syscall_func syscalls[20];
static void syscall_handler (struct intr_frame *);


void exit_ret(int ret_status){
  thread_current()->ret_status = ret_status;
  thread_exit();
}

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

struct file_node * file_find(struct  list *file_list,int fd){
  struct list_elem *e;
  struct file_node * fn = NULL;
  for(e = list_begin(file_list);e != list_end(file_list);e = list_next(e)){
    fn = list_entry(e,struct file_node,file_elem);
    if (fn->fd ==fd){{
      return fn;
    }
  }
  return NULL;
}

static void
syscall_handler (struct intr_frame *f) 
{
  // retrieve number and args 
  int number = *(int*)(f->esp);
  if ((number > SYS_INUMBER) || (number < 0)){
    exit_ret(-1);   
  }
  if (syscalls[number]==NULL) exit_ret(-1)
  // pass on to syscalls
  syscalls[number](f);
}

// implement syscall functions
void sys_halt(struct intr_frame *f){
  shutdown_power_off();
}

void sys_exit(struct intr_frame *f){
  exit_ret(*(((int*)f->esp)+1));
}

void sys_exec(struct intr_frame *f){
  char* file_name = (char*)(((int*)f->esp)+1);
  f->eax = process_execute(file_name);
}

void sys_wait(struct intr_frame *f){
  pid_t pid = (pid_t)*(((int*)f->esp)+1);
  if (pid != -1)
  {
    f->eax = process_wait(pid);
  } else {
    f->eax = -1;
  }
}

void sys_create(struct intr_frame *f){
  char* file_name = (char*)(((int*)f->esp)+1);
  off_t file_size = *(((int*)f->esp)+2);
  f->eax = filesys_create(file_name,file_size);
}
void sys_remove(struct intr_frame *f){
  char* file_name = (char*)(((int*)f->esp)+1);
  f->eax = filesys_remove(file_name);
}

void sys_open(struct intr_frame *f){
  char* file_name = (char*)(((int*)f->esp)+1);
  if (file_name==NULL) {
    f->eax = -1;
    exit_ret(-1);
  }
  struct file* open_file = filesys_open(file_name); 
  if (open_file){
    // file node create when open file successfully.
    struct file_node* fn = (struct file_node*)malloc(sizeof(struct file_node));
    fn->f = open_file;
    
    // file!!!
    list_push_back(thread_current()->fds,&fn->file_elem);
    f->eax = fn->fd;    //                                       
  }
  else{
    f->eax = -1;
  }
}
void sys_filesize(struct intr_frame *f){
  int fd = *(((int*)(f->esp))+1);
  struct file_node * openf = file_find(&thread_current()->fds,fd);
  if (openf){
    f->eax = file_length(openf->f);
  } else {
    f->eax = -1;
  }
}

void sys_read(struct intr_frame *f){
  int fd = *(((int*)(f->esp))+1);
  char* buffer = (char*)(*(((int*)(f->esp))+2));
  unsigned size = *(((int*)(f->esp))+3);

  if (fd == STDIN_FILENO){
    for(int i = 0;i < size;i++){
      buffer[i] = input_getc();
    }
    f->eax = size;
  } else {
    struct file_node * openf = file_find(&thread_current()->fds,fd);
    if (openf){
      f->eax = file_read(openf->f,buffer,size);
    } else {
      f->eax = -1;
    }
  }
}

void sys_write(struct intr_frame *f){
  //check user valid address
  int fd = *(((int*)(f->esp))+1);
  char* buffer = (char*)(*(((int*)(f->esp))+2));
  unsigned size = *(((int*)(f->esp))+3);
  if (fd == STDOUT_FILENO){
    putbuf(buffer,size);
    f->eax = 0;
  }
  else{
    struct thread* cur = thread_current();
    struct file_node* fn = get_file(cur,fd); //file_node undefined!!!
    if(fn == NULL){
      f->eax = 0;
      return;
    }
    f->eax = file_write(fn->f,buffer,size); // fn->f is a file.
  }
}

void sys_seek(struct intr_frame *f){
  int fd = *(((int*)(f->esp))+1);
  unsigned position = *(((int*)(f->esp))+3);
  struct file_node openf = file_find(&thread_current()->fds, fd);
  file_seek(openf->f,position);
}

void sys_tell(struct intr_frame *f){
  int fd = *(((int*)f->esp)+1);
  struct file_node * openf = file_find(&thread_current()->fds, fd);
  if (openf){
    f->eax = file_tell(openf->f);
  } else {
    f->eax = -1;
  }
}

void sys_close(struct intr_frame *f){
  int fd = *(((int*)f->esp)+1);
  struct file_node * openf = file_find(&thread_current()->fds, fd);
  if (openf){
    file_close(openf->file);
    list_remove(&openf->file_elem);
    free(openf);// used to free struct openf
  } 
}

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

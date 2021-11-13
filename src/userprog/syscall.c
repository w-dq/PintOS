#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"



typedef void (*syscall_func)(struct intr_frame * UNUSED);
syscall_func syscalls[20];
int max_files = 200;

static void syscall_handler (struct intr_frame *);
struct file_node * file_find(struct list *,int);
void exit_ret(int);

void 
exit_ret(int ret_status)
{
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
  syscalls[SYS_OPEN] = sys_open_file;
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

/* find file_node representing fd. */ //checked
struct file_node * 
file_find(struct list *file_list,int fd)
{
  struct list_elem *e;
  struct file_node * fn = NULL;
  for(e = list_begin(file_list); e != list_end(file_list); e = list_next(e)){
    fn = list_entry(e,struct file_node,elem);
    if (fn->fd == fd){
      return fn;
    }
  }
  return NULL;
}

static void 
syscall_handler (struct intr_frame *f)
{ 
  if (!is_user_vaddr(f->esp)) exit_ret(-1);
  int number = *(int*)(f->esp);
  if ((number > SYS_INUMBER) || (number < 0)){
    exit_ret(-1);   
  }
  if (syscalls[number]==NULL) exit_ret(-1);
  // pass on to syscalls
  syscalls[number](f);
}

// implement syscall functions
void 
sys_halt(struct intr_frame *f UNUSED)
{
  shutdown_power_off();
}

void 
sys_exit(struct intr_frame *f)
{
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  thread_current()->ret_status = *(int*)(f->esp+4);
  f->eax = 0;
  thread_exit();
  //exit_ret(*(((int*)f->esp)+1));
}

void 
sys_exec(struct intr_frame *f)
{
  //need check valid user address
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  if (!is_user_vaddr(*(int*)(f->esp+4))) exit_ret(-1);
  char* file_name = (char*)(*(int*)(f->esp+4));
  if (file_name==NULL){
    f->eax = -1;
  }
  else{
    char* new_file = (char*)malloc(sizeof(strlen(file_name)+1));
    memcpy(new_file,file_name,strlen(file_name)+1);
    tid_t tid = process_execute(new_file);
    struct thread* thd = get_thread_by_tid(tid);
    f->eax = tid;
    free(new_file);
  }
}

void 
sys_wait(struct intr_frame *f)
{
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  tid_t pid = *(tid_t*)(f->esp+4);
  if (pid != -1)
  {
    f->eax = process_wait(pid);
  } else {
    f->eax = -1;
  }
}

void 
sys_create(struct intr_frame *f)
{
  if (!is_user_vaddr(f->esp+8)) exit_ret(-1);
  if (!is_user_vaddr((*(int*)(f->esp+4)))) exit_ret(-1);
  
  char* file_name = (char*)(*(int*)(f->esp+4));
  off_t file_size = *(off_t*)(f->esp+8);
  if (file_name == NULL){
    f->eax = -1;
    exit_ret(-1);
  } 
  lock_acquire(&file_lock);
  bool success = filesys_create(file_name,file_size);
  f->eax = success;
  lock_release(&file_lock);
}

void 
sys_remove(struct intr_frame *f)
{
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  if (!is_user_vaddr((*(int*)(f->esp+4)))) exit_ret(-1);
  lock_acquire(&file_lock);
  char* file_name = (char*)(*(int*)(f->esp+4));
  f->eax = filesys_remove(file_name);
  lock_release(&file_lock);
}

void 
sys_open_file(struct intr_frame *f)
{ 
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  if (!is_user_vaddr((*(int*)(f->esp+4)))) exit_ret(-1);
  char* file_name = (char*)(*(int*)(f->esp+4));
  if (file_name == NULL) {
    f->eax = -1;
    exit_ret(-1);
  }
  lock_acquire(&file_lock);
  struct file* open_file = filesys_open(file_name); 
  lock_release(&file_lock);
  if (open_file && (thread_current()->open_file_num < max_files)){
    // file node create when open file successfully.
    struct file_node* fn = (struct file_node*)malloc(sizeof(struct file_node));
    fn->f = open_file;
    thread_current()->max_fd++;    // next_handle == max_fd
    fn->fd =  thread_current()->max_fd;
    
    list_push_back(&(thread_current()->open_file_list),&(fn->elem));
    thread_current()->open_file_num ++;
    f->eax = fn->fd;                         
  }
  else{
    f->eax = -1;
  }
}

void 
sys_filesize(struct intr_frame *f)  ////////////
{//checked
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  int fd = *(int*)(f->esp+4);
  struct file_node * openf = file_find(&(thread_current()->open_file_list),fd);
  if (openf){
    lock_acquire(&file_lock);
    f->eax = file_length(openf->f);
    lock_release(&file_lock);
  } else {
    f->eax = -1;
  }
}

void 
sys_read(struct intr_frame *f)
{   
  if (!is_user_vaddr(f->esp+12)) exit_ret(-1);
  if (!is_user_vaddr((*(int*)(f->esp+8)))) exit_ret(-1);
  int fd = *(int*)(f->esp+4);
  char* buffer = (char*)(*(int*)(f->esp+8));
  unsigned size = *(unsigned*)(f->esp+12);

  if (fd == 0){
    for(int i = 0;i < (int)size;i++){
      buffer[i] = input_getc();
    }
    f->eax = size;
  } else {
    struct file_node * openf = file_find(&(thread_current()->open_file_list),fd);
    if (openf){
      lock_acquire(&file_lock);
      f->eax = file_read(openf->f,buffer,size);
      lock_release(&file_lock);
    } else {
      f->eax = -1;
    }
  }
}

void 
sys_write(struct intr_frame *f)
{ 
  //check user valid address
  if (!is_user_vaddr(f->esp+12)) exit_ret(-1);
  if (!is_user_vaddr((*(int*)(f->esp+8)))) exit_ret(-1);
  int fd = *(int*)(f->esp+4);
  char* buffer = (char*)(*(int*)(f->esp+8));
  unsigned size = *(unsigned*)(f->esp+12);
  if (fd == 1){
    putbuf(buffer,size);
    f->eax = size;
  }
  else{
    struct thread* cur = thread_current();
    struct file_node* fn = file_find(&(cur->open_file_list),fd); 
    if(fn == NULL){
      f->eax = 0;
      return;
    }
    lock_acquire(&file_lock);
    f->eax = file_write(fn->f,buffer,size); 
    lock_release(&file_lock);
  }
}

void 
sys_seek(struct intr_frame *f)
{ 
  if (!is_user_vaddr(f->esp+8)) exit_ret(-1);
  int fd = *(int*)(f->esp+4);
  unsigned position = *(unsigned*)(f->esp+8);
  struct file_node* openf = file_find(&(thread_current()->open_file_list), fd);
  lock_acquire(&file_lock);
  file_seek(openf->f,position);
  lock_release(&file_lock);
}

void 
sys_tell(struct intr_frame *f)
{
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  int fd = *(int*)(f->esp+4);
  struct file_node * openf = file_find(&(thread_current()->open_file_list), fd);
  if (openf){
    lock_acquire(&file_lock);
    f->eax = file_tell(openf->f);
    lock_release(&file_lock);
  } 
  else {
    f->eax = -1;
  }
}

void 
sys_close(struct intr_frame *f)
{
  if (!is_user_vaddr(f->esp+4)) exit_ret(-1);
  int fd = *(int*)(f->esp+4);
  struct file_node * openf = file_find(&(thread_current()->open_file_list), fd);
  if (openf){
    lock_acquire(&file_lock);
    file_close(openf->f);
    lock_release(&file_lock);
    list_remove(&(openf->elem));
    free(openf);// used to free struct openf
  } 
}

// the following functions are unused in proj2
void sys_mmap(struct intr_frame *f UNUSED){
  printf("sys_mmap");
}
void sys_munmap(struct intr_frame *f UNUSED){
  printf("sys_munmap");
}

void sys_chdir(struct intr_frame *f UNUSED){
  printf("sys_chdir");
}
void sys_mkdir(struct intr_frame *f UNUSED){
  printf("sys_mkdir");
}
void sys_readdir(struct intr_frame *f UNUSED){
  printf("sys_readdir");
}
void sys_isdir(struct intr_frame *f UNUSED){
  printf("sys_isdir");
}
void sys_inumber(struct intr_frame *f UNUSED){
  printf("sys_inumber");
}

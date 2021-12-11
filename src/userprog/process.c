#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#include "vm/frame.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

static bool load_segment_lazily (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
bool mmfile_less(const struct hash_elem*, const struct hash_elem*, void*);
unsigned mmfile_hash(const struct hash_elem*, void*);
mapid_t mmfiles_insert (void*, struct file*, int32_t);
static void free_mmfiles_entry (struct hash_elem*, void*);
void free_mmfiles (struct hash*);


struct ret_data{
  int tid;
  int ret;
  struct list_elem elem;
};

struct mmfile
{
  mapid_t mapid;
  struct file* file;
  void * start_addr;
  unsigned pg_cnt; 
  struct hash_elem elem;
};

static void mmfiles_free_entry (struct mmfile*);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *fn_parsed;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = (char*) malloc(strlen(file_name)+1);
  fn_parsed = (char*) malloc(strlen(file_name)+1);
  if (fn_copy == NULL)
    return TID_ERROR;
  memcpy (fn_copy, file_name, strlen(file_name)+1);
  memcpy (fn_parsed, fn_copy, strlen(file_name)+1);

  /* Create a new thread to execute FILE_NAME. */
  char* context = NULL;
  char* token = strtok_r(fn_parsed, " ", &context);
  tid = thread_create (token, PRI_DEFAULT, start_process, fn_copy);
  
  if (tid == TID_ERROR){
    free(fn_copy);
    return TID_ERROR;
  }
  sema_down(&thread_current()->load_wait);
  if(!thread_current()->load_status) return TID_ERROR;
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  
  hash_init (&thread_current()->suppl_page_table, suppl_pt_hash, suppl_pt_less, NULL);
  hash_init(&thread_current()->mmfiles, mmfile_hash,mmfile_less,NULL);
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  char* context = NULL;
  char* token = strtok_r(file_name, " ", &context);

  success = load (token, &if_.eip, &if_.esp);
  lock_acquire(&file_lock);
  thread_current()->self_elf = filesys_open(token);
  if(thread_current()->self_elf != NULL) {
    file_deny_write(thread_current()->self_elf);
  }
  lock_release(&file_lock);

  thread_current()->parent->load_status = success;
  sema_up(&thread_current()->parent->load_wait);
  /* If load failed, quit. */
  if (!success){
    free(file_name);
    exit_ret(-1);
  }
  
  int argc = 0;
  int argv[128];
  do {
    if_.esp -= (strlen(token) + 1);
    memcpy(if_.esp, token, strlen(token) + 1);
    argv[argc] = (int)if_.esp;
    argc++;

    token = strtok_r(NULL, " ", &context);
  } while (token != NULL);

  /* word alignment */
  int zero = 0;
  while(((int)(if_.esp)) % 4 != 0){
    if_.esp--;
  }

  /* string end zero */
  if_.esp -= 4;
  memcpy(if_.esp,&zero,4);

  /* push argv value */
  for(int i = argc - 1; i >= 0; i--){
    if_.esp -= 4;
    memcpy(if_.esp, &argv[i], 4);
  }

  int argv_head = (int)if_.esp;
  if_.esp -= 4;
  memcpy(if_.esp, &argv_head, 4);
  if_.esp -= 4;
  memcpy(if_.esp, &argc, 4);

  /* fake return address */
  if_.esp -= 4;
  memcpy(if_.esp,&zero,4);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */

static int
get_ret_from_child(struct thread* cur, tid_t child_tid){
  int ret=-1;
  struct list_elem* e;
  for (e=list_begin(&(cur->child_ret_list)); e!=list_end(&(cur->child_ret_list));e=list_next(e)){
    struct ret_data* rd = list_entry(e,struct ret_data, elem);
    if (rd->tid == child_tid){
      ret = rd->ret;
      rd->ret = -1;
      break;
    }
  }
  return ret;
}

int
process_wait (tid_t child_tid) 
{
  struct thread* cur = thread_current();
  struct thread* child = get_thread_by_tid(child_tid); 
  if (child == NULL||child->status == THREAD_DYING||child->save_ret){ //? finished?
    return get_ret_from_child(cur,child_tid);
  }
  else{
    cur->is_wait = true;
    sema_down(&(child->sema_wait)); //? multiple
    return get_ret_from_child(cur,child_tid);
  }
}


void
record_ret(struct thread* t, int tid, int ret){
  struct ret_data* rd = (struct ret_data*)malloc(sizeof(struct ret_data));
  rd->tid = tid;
  rd->ret = ret;
  list_push_back(&t->child_ret_list,&rd->elem);
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      printf("%s: exit(%d)\n", cur->name, cur->ret_status);

      while(!list_empty(&cur->open_file_list))
      {
        struct file_node * fn = list_entry(list_pop_front(&cur->open_file_list),struct file_node, elem);

        file_close(fn->f);

        free(fn);
      }

      cur->open_file_num = 0;

      record_ret(cur->parent,cur->tid, cur->ret_status);
      cur->save_ret = true;

      if(cur->parent!=NULL && cur->parent->is_wait){
          sema_up(&cur->sema_wait);
      }

      while(!list_empty(&cur->child_ret_list)){
        struct ret_data* rd = list_entry(list_pop_front(&cur->child_ret_list),struct ret_data, elem);
        free(rd);
      }

      file_close(cur->self_elf);

      free_suppl_pt (&cur->suppl_page_table); 
      
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable) UNUSED;

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  lock_acquire(&file_lock);
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment_lazily (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  lock_release(&file_lock);
  return success;
}

/* load() helpers. */
static bool
load_segment_lazily (struct file *file, off_t ofs, uint8_t *upage,
		     uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Add an file suuplemental page entry to supplemental page table */ 
      if (!suppl_pt_insert_file (file, ofs, upage, page_read_bytes,
                                 page_zero_bytes, writable))
	return false;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
  
}


static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  struct frame* fm;

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      // uint8_t *kpage = palloc_get_page (PAL_USER);
      uint8_t *kpage = frame_allocate (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          frame_free (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          frame_free (kpage);
          return false; 
        }
              
      fm = get_frame(kpage);
      fm->evictable = true;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;
  struct frame* fm;

  kpage = frame_allocate (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      fm = get_frame(kpage);
      fm->evictable = true;
      if (success)
        *esp = PHYS_BASE;
      else
        frame_free (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* hash function for mmfile */
unsigned
mmfile_hash(const struct hash_elem *e, void *aux UNUSED){
  const struct mmfile *p = hash_entry (e, struct mmfile, elem);
  return hash_bytes (&p->mapid, sizeof p->mapid);
}

/* hash required functionality */
bool 
mmfile_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  const struct mmfile* fa = hash_entry (a, struct mmfile, elem);
  const struct mmfile* fb = hash_entry (b, struct mmfile, elem);
  return (fa->mapid < fb->mapid);
}

/* adding and entry to the memory file table and suppl pt. */
mapid_t 
mmfiles_insert(void *addr, struct file* file, int32_t len){
  struct thread* cur = thread_current();
  struct mmfile *mmf;
  struct hash_elem* result;

  mmf = calloc (1, sizeof(mmf));
  if (mmf == NULL) return -1;
  
  mmf->mapid = cur->mapid_allocator++;
  mmf->file = file;
  mmf->start_addr = addr;
  
  int offset = 0;
  int pg_cnt = 0;
  int tmp_len = len;
  while (tmp_len > 0)
    {
      size_t read_bytes = tmp_len < PGSIZE ? tmp_len : PGSIZE; 
      if (!suppl_pt_insert_mmf(file, offset, addr, read_bytes)) return -1;
      offset += PGSIZE;
      tmp_len -= PGSIZE;
      addr += PGSIZE;
      pg_cnt++;
    }

  mmf->pg_cnt = pg_cnt; 
  result = hash_insert (&(cur->mmfiles), &mmf->elem);
  if (result != NULL) return -1;
  return mmf->mapid; 
}

/* removing an entry from mmf table, also remove from spt
   by calling mmfiles_free_entry. */
void mmfiles_remove (mapid_t mapping){
  struct thread *cur = thread_current ();
  struct mmfile mmf;
  struct mmfile *mmf_ptr;
  struct hash_elem *he;

  mmf.mapid = mapping;

  he = hash_delete (&cur->mmfiles, &mmf.elem);
  if (he != NULL)
    {
      mmf_ptr = hash_entry (he, struct mmfile, elem);
      mmfiles_free_entry (mmf_ptr);
    }
}

/* used in mmfiles_remove and free_mmfiles_entry. 
   checks for dirty pages and writing back to files 
   free from suppl_pte. */
static void
mmfiles_free_entry (struct mmfile* mmf_ptr)
{
  struct thread *t = thread_current ();
  struct hash_elem *he;
  int pg_cnt;
  struct suppl_pte spte;
  struct suppl_pte *spte_ptr;
  int offset;

  pg_cnt = mmf_ptr->pg_cnt;
  offset = 0;
  while (pg_cnt-- > 0)
  {
      spte.usr_vadr = mmf_ptr->start_addr + offset;
      he = hash_delete (&t->suppl_page_table, &spte.elem);
      if (he != NULL)
	{
	  spte_ptr = hash_entry (he, struct suppl_pte, elem);
	  if (spte_ptr->is_loaded
	      && pagedir_is_dirty (t->pagedir, spte_ptr->usr_vadr))
	    {
	      lock_acquire (&file_lock);
	      file_seek (spte_ptr->data.mmf_page.file, 
			  spte_ptr->data.mmf_page.ofs);
	      file_write (spte_ptr->data.mmf_page.file, 
			  spte_ptr->usr_vadr,
			  spte_ptr->data.mmf_page.read_bytes);
	      lock_release (&file_lock);
	    }
	  free (spte_ptr);
	}
      offset += PGSIZE;
    }

  lock_acquire (&file_lock);
  file_close (mmf_ptr->file);
  lock_release (&file_lock);

  free (mmf_ptr);
}

/* destroy the memory map file table */
void 
free_mmfiles (struct hash *mmfiles)
{
  hash_destroy (mmfiles, free_mmfiles_entry);
}

/* free resource for each entry in memory map file table */
static void
free_mmfiles_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct thread *cur = thread_current ();
  struct mmfile *mmf;

  mmf = hash_entry (e, struct mmfile, elem);
  hash_delete (&cur->mmfiles, &mmf->elem);
  mmfiles_free_entry (mmf);
}
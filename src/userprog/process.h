#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void mmfiles_remove (mapid_t);
mapid_t mmfiles_insert(void*, struct file*, int32_t);

void record_ret(struct thread* , int, int);

#endif /* userprog/process.h */

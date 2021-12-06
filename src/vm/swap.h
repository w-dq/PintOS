#ifndef VM_SWAP_H
#define VM_SWAP_H

#define SWAP_ERROR SIZE_MAX


void vm_swap_init (void);
size_t vm_swap_out (const void *);
void vm_swap_in (size_t, void *);
void vm_clear_swap_slot (size_t);

#endif
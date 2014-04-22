/* Swap Space Header */

#ifndef _SWAPSPACE_H_
#define _SWAPSPACE_H_

#include <types.h>
#include <thread.h>
#include <current.h>
#include <vnode.h>
#include <addrspace.h>

/* Initialize the hdd for page swapping. Hdd selected in swapspace.c */
int swapspace_init(void);

/* Evict the specified page; page MUST be clean. Page is removed from memory*/
int evict_page(struct addrspace* as, vaddr_t va);

/* Swap the specified page out to disk; maked page clean but
	does NOT evict the page. */
int swapout_page(struct addrspace* as, vaddr_t va);

/* Swap the specified page back into memory. */
int swapin_page(struct addrspace* as, vaddr_t va);

#endif /* _SWAPSPACE_H_ */


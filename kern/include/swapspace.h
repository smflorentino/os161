/* Swap Space Header */

#ifndef _SWAPSPACE_H_
#define _SWAPSPACE_H_

#include <types.h>
#include <thread.h>
#include <current.h>
#include <vnode.h>
#include <addrspace.h>

#define SWAP_MAX 512//1024	// Maximum number of pages that can be swapped to disk

#define SIND_TO_DISK(swap_index) ( (swap_index * PAGE_SIZE) )

/* Entry for swap space array, which indicates what page is in a given swap
   location on disk. */
//struct swap_entry {
//	struct page *page;
//};

/* Initialize the hdd for page swapping. Hdd selected in swapspace.c */
int swapspace_init(void);

/* Evict the specified page; page MUST be clean. Page is removed from memory*/
int evict_page(struct page* page);

/* Swap the specified page out to disk; maked page clean but
	does NOT evict the page. */
int swapout_page(struct page* page);

/* Swap the specified page back into memory. */
int swapin_page(struct page* page);

#endif /* _SWAPSPACE_H_ */



/* Swap Space Setup and Functions */

#include <types.h>
#include <thread.h>
#include <vnode.h>
#include <lib.h>
#include <swapspace.h>
#include <vfs.h>
#include <kern/fcntl.h>



/* Pointer to swap hdd, lhd1raw: */
static struct vnode* swapspace;
static char swap_disk[] = "lhd1raw:";

int swapspace_init(void)
{
	int result;

	result = vfs_open(swap_disk, O_RDWR, 0, &swapspace);
	KASSERT(swapspace != NULL);

	if (result) {
		kprintf("Something has gone wrong with the swap disk: err %d\n",result);
	}
	else {
		kprintf("Mounted lhd1raw as swapspace.\n");
	}

	return result;
}

/* Evict a CLEAN page from memory. Called by page swapping algorithm */

//void evict_page(void/*as, va*/)
//{
	// check coremap do i have?
	// Lock coremap

	// Double check that the page exists
	// Double check that page is clean.

	// Shootdown the TLB

	// Update the Page Table to list the page as swapped

	// Update the coremap to list the physical page as free
	
	// Unlock core map

//	return;
//}

//void swapoout_page(void/*as, va*/)
//{
	// check coremap lock do i have?
	//lock coremap

	// double check that page exists
	// double check that page is dirty

	// mark page as SWAPPING IN PROGRESS
	// check swap space to see if this page is already there
		//if present, swap to location of existing page
		//if not, find free swap space
			//Update swap bit map for new location as occupied
			//Write zeros to swap?
			// Write page to disk
		//if no swap avialable, panic!!!
	// release coremap lock
	//	sleep until swap to disk completes, so that others can run
	// grab coremap lock
	// mark page as CLEAN
	// release coremap lock

//	return;
//}

//void swapin_page(void/*as,va*/)
//{
	// check coremap do i have?
	// lock cremap

	// double check that page exists
	// double check that page is swapped

	// Check for free space in memory
		//if space found, continue with swap in
		// if not free space:
			// Pick page to swap out
			//swapout_page()
			//evict()

	// write zeros to the evicted physical page
	// mark page as SWAPPING IN PROGRESS
		// swap in the page
	// release coremap lock
	// sleep until swap in completes
	// lock coremap
	// mark page as DIRTY
	//release coremap lock

	//return;
//}





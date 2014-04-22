
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

/* Selection of the hdd to use. 
	Using hdd1 cuz we are rebels. */
//static char swap_disk[] = "lhd0raw:";
static char swap_disk[] = "lhd1raw:";

int swapspace_init(void)
{
	int result;

	// Open the hard drive for reading/writing.
	result = vfs_open(swap_disk, O_RDWR, 0, &swapspace);
	KASSERT(swapspace != NULL);

	// Make it known if all went well.
	if (result) {
		kprintf("Something has gone wrong with the swap disk: err %d\n",result);
	}
	else {
		kprintf("Mounted %s as the swap disk.\n", swap_disk);
	}

	return result;
}

/* Contains uio setup and execution for writting page to disk. */
static
int write_page(int swap_index)
{
	(void)swap_index;
	int result = 0;

	return result;
}

/* Contains uio setup and execution for reading page from disk. */
static
int read_page(int swap_index/*needs mem location to write to*/)
{
	(void)swap_index;
	int result = 0;

	return result;
}

/* Evict a CLEAN page from memory. Called by page swapping algorithm */

int evict_page(struct addrspace* as, vaddr_t va)
{
	(void)as;
	(void)va;

	int result = 0;

	// check coremap do i have?
	// Lock coremap

	// Double check that the page exists
	// Double check that page is clean.

	// Shootdown the TLB

	// Update the Page Table to list the page as swapped

	// Update the coremap to list the physical page as free
	
	// Unlock core map

	return result;

}

/* Swap the specified page out to disk; maked page clean but
	does NOT evict the page. */
int swapout_page(struct addrspace* as, vaddr_t va)
{
	(void)as;
	(void)va;	

	int result = 0;
	int swap_index;

	write_page(swap_index);

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

	return result;
}

/* Swap the specified page back into memory. */
int swapin_page(struct addrspace* as, vaddr_t va)
{
	(void)as;
	(void)va;

	int result = 0;
	int swap_index;

	read_page(swap_index);

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

	return result;
}





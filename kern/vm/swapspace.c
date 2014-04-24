
/* Swap Space Setup and Functions */

#include <types.h>
#include <thread.h>
#include <vnode.h>
#include <lib.h>
#include <swapspace.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <vm.h>
#include <synch.h>
#include <uio.h>

/* Pointer to swap hdd, lhd1raw: */
static struct vnode* swapspace;

/* Selection of the hdd to use. 
	Using hdd1 cuz we are rebels. */
//static char swap_disk[] = "lhd0raw:";
static char swap_disk[] = "lhd1raw:";

// Array of swap entry pointers. Walk this to find your swapped page.
// Locate the page by comparing the page structure.
static struct page *swap_table[SWAP_MAX];

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

	// Initialize the swap table to be empty; no pages in swap yet!
	// TODO: is the NULL init correct below?
	for (int i = 0; i < SWAP_MAX; i++) {
		//swap_table[i]->page = NULL;
		swap_table[i] = NULL;
	}

	return result;
}

/* Contains uio setup and execution for writting page to disk. */
static
int write_page(int swap_index, paddr_t page)
{
	//(void)swap_index;
	//(void)page;

	int result = 0;

	struct iovec iov;
	struct uio page_write_uio;

	off_t pos = SIND_TO_DISK(swap_index);

	uio_kinit(&iov, &page_write_uio, (void*)page, PAGE_SIZE, pos, UIO_WRITE);
	result = VOP_WRITE(swapspace, &page_write_uio);

	return result;
}

/* Contains uio setup and execution for reading page from disk. */
static
int read_page(int swap_index, paddr_t page)
{
	//(void)swap_index;
	//(void)page;

	int result = 0;

	struct iovec iov;
	struct uio page_read_uio;

	off_t pos = SIND_TO_DISK(swap_index);

	uio_kinit(&iov, &page_read_uio, (void*)page, PAGE_SIZE, pos, UIO_READ);
	result = VOP_READ(swapspace, &page_read_uio);

	return result;
}

/* Evict a CLEAN page from memory. Called by page swapping algorithm */

int evict_page(struct page* page)
{
	//(void)page;

	volatile int result = 0;	// Incase we ever want to pass something back.

	// check coremap do i have?
	// Lock coremap
	//bool holdlock = lock_do_i_hold(core_map_lock);
	//if(!holdlock) {
	//	lock_acquire(core_map_lock);
	//}

	// Double check that the page exists
	// Double check that page is clean.
	KASSERT(page->state == CLEAN);

	// Shootdown the TLB
	//vm_tlbshootdown();

	// Update the Page Table to list the page as swapped
	struct page_table *pt = pgdir_walk(page->as,page->va,false);
	int pt_index = VA_TO_PT_INDEX(page->va);
	pt->table[pt_index] |= PTE_SWAP;		 // Flips the bit to indicate swapped
	KASSERT(pt->table[pt_index] & PTE_SWAP); // Check that the bit was set correctly.

	// Update the coremap to list the physical page as free
	free_kpages(page->va);

	// Unlock core map
	//lock_release(core_map_lock);

	return result;

}

/* Swap the specified page out to disk; maked page clean but
	does NOT evict the page. */
int swapout_page(struct page* page)
{	

	int result = 0;
	int swap_index;

	// check coremap lock do i have?
	//lock coremap
	//bool holdlock = lock_do_i_hold(core_map_lock);
	//if(!holdlock) {
	//	lock_acquire(core_map_lock);
	//}

	// double check that page exists
	// double check that page is dirty
	KASSERT(page->state == DIRTY);

	// mark page as SWAPPING IN PROGRESS

	// Check swap space to see if this page is already there...
	swap_index = -1;	// -1 is an invalid index
	for (int i=0; i < SWAP_MAX; i++) {
		if (swap_table[i] == page) {
			swap_index = i;
			break;
		}
	}

	// ...if not, find free swap space.
	if (swap_index < 0) {
		for (int i=0; i < SWAP_MAX; i++) {
			if (swap_table[i] == NULL) {
				swap_index = i;
				break;
			}
		}
	}

	// Swap didn't exist and there are no free swaps left; oh dear...
	// If no swap avialable, panic!!!
	if (swap_index < 0) {
		panic("Out of disk space!!!");
	}

	// Write zeros to swap?

	// Update swap bit map for new location as occupied
	// Update swap table so we can find this page later
	swap_table[swap_index] = page;
	// Write page to disk
	write_page(swap_index, page->pa);

	// Unlock core map to sleep
	//lock_release(core_map_lock);

	// sleep until swap to disk completes, so that others can run
	// grab coremap lock

	// mark page as CLEAN
	page->state = CLEAN;

	// release coremap lock
	//lock_release(core_map_lock);

	return result;
}

/* Swap the specified page back into memory. */
int swapin_page(struct page* page)
{
	(void)page;

	int result = 0;
	int swap_index;

	read_page(swap_index, page->pa);

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





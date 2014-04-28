
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

/* Pick swapping implementation below; either swapfile or raw style. Comment out
	the one to not implement. */
//#define SWAP_FILE	// Selects swapfile version of swapping
//#define SWAP_RAW	// Selects raw swap disk version of swapping

/* Pointer to swap hdd, lhd1raw: */
static struct vnode* swapspace;

// Array of swap entry pointers. Walk this to find your swapped page.
// Locate the page by comparing the page structure.
static struct swap_entry swap_table[SWAP_MAX];

/***********************************************************************************
************************************************************************************
***********************************************************************************/

#if SWAP_RAW

/* Selection of the hdd to use. 
	Using hdd1 cuz we are rebels. */
//static char swap_disk[] = "lhd0raw:";
static char swap_disk_raw[] = "lhd1raw:";

/* Initialization of the swap disk and the swap table. This one is for the RAW disk,
	the swapfile version is below. */
int swapspace_init(void)
{
	int result;

	// Open the hard drive for reading/writing.
	//result = vfs_open("lhd0raw:", O_RDWR, 0, &swapspace);
	//char open_disk[10];
	//strcpy(open_disk,swap_disk);
	result = vfs_open(swap_disk_raw, O_RDWR, 0, &swapspace);
	KASSERT(swapspace != NULL);

	// Make it known if all went well.
	if (result) {
		kprintf("Something has gone wrong with the swap disk: err %d\n",result);
	}
	else {
		kprintf("Opened %s as the swap disk.\n", swap_disk);
	}

	// Initialize the swap table to be empty; no pages in swap yet!
	// TODO: is the NULL init correct below?
	for (int i = 0; i < SWAP_MAX; i++) {
		//swap_table[i]->page = NULL;
		swap_table[i].as = NULL;
	}

/*
	// Testing raw disk writes
	int buff1[1024];
	int buff2[1024];
	for(int i = 0; i < 1024; i++) {
		buff1[i] = 3;
		buff2[i] = 0;
	}
	struct iovec iov_write;
	struct iovec iov_read;
	struct uio page_write_uio;
	struct uio page_read_uio;

	off_t pos = 0x100; //SIND_TO_DISK(swap_index);

	uio_kinit(&iov_write, &page_write_uio, buff1, PAGE_SIZE, pos, UIO_WRITE);
	result = VOP_WRITE(swapspace, &page_write_uio);

	int j = 0;
	while(page_write_uio.uio_resid != 0) {
		j++;
	}

	kprintf("j = %d\n", j);

	pos = 0x100;

	uio_kinit(&iov_read, &page_read_uio, buff2, PAGE_SIZE, pos, UIO_READ);
	result = VOP_READ(swapspace, &page_read_uio);

	int k = 0;
	while(page_write_uio.uio_resid != 0) {
		k++;
	}

	kprintf("k = %d\n", k);

	kprintf("result buffer = %d\n", buff2[10]);
*/
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
	//result = VOP_WRITE(swapspace, &page_write_uio);

	// Need to wait for write to finish?

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

	// Need to wait for read to finish?

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
	struct tlbshootdown tlb;
	tlb.ts_vaddr = page->va;
	vm_tlbshootdown(&tlb);

	// Update the Page Table to list the page as swapped
	struct page_table *pt = pgdir_walk(page->as,page->va,false);
	int pt_index = VA_TO_PT_INDEX(page->va);
	pt->table[pt_index] |= PTE_SWAP;		 // Flips the bit to indicate swapped
	KASSERT(pt->table[pt_index] & PTE_SWAP); // Check that the bit was set correctly.

	// Update the coremap to list the physical page as free
	//free_kpages(page->va);

	vaddr_t page_location = PADDR_TO_KVADDR(page->pa);
	free_kpages(page_location);

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

	// Only handling singe pages right now
	KASSERT(page->npages == 1);

	// mark page as SWAPPING IN PROGRESS

	// Check swap space to see if this page is already there...
	swap_index = -1;	// -1 is an invalid index
	for (int i=0; i < SWAP_MAX; i++) {
		if (swap_table[i].as == page->as && swap_table[i].va == page->va) {
			swap_index = i;
			break;
		}
	}

	// ...if not, find free swap space.
	if (swap_index < 0) {
		for (int i=0; i < SWAP_MAX; i++) {
			if (swap_table[i].as == NULL) {
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

	// Lock the swap_table lock, since it's shared

	// Update swap bit map for new location as occupied
	// Update swap table so we can find this page later
	swap_table[swap_index].as = page->as;
	swap_table[swap_index].va = page->va;

	// Write page to disk
	write_page(swap_index, page->pa);

	// Unlock the swap_lock

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
int swapin_page(struct addrspace* as, vaddr_t va, struct page* page)
{

	// Check for free space in memory (finding the free space is the swap alg's job)
	KASSERT(page->as == NULL);
	KASSERT(as != NULL);

	volatile int result = 0;	// Incase we want to pass infor back up.
	int swap_index;



	// check coremap do i have?
	// lock cremap

	// double check that page exists
	// double check that page is swapped
	struct page_table *pt = pgdir_walk(as,va,false);
	int pt_index = VA_TO_PT_INDEX(va);
	KASSERT(pt->table[pt_index] & PTE_SWAP); // Check that the page is swapped.

	// lock the swap table

	// Locate the swapped page in the swap table
	swap_index = -1;	// -1 is an invalid index
	for (int i=0; i < SWAP_MAX; i++) {
		if (swap_table[i].as == as && swap_table[i].va == va) {
			swap_index = i;
			break;
		}
	}

	// Unlock swap table

	// ...if not, no swapped page exists...
	if(swap_index < 0) {
		panic("Tried to swap in a non-existant page.");
	}



		// (below will be job of swapping algorithm)
		//if space found, continue with swap in
		// if not free space:
			// Pick page to swap out
			//swapout_page()
			//evict()
	// write zeros to the evicted physical page
	// mark page as SWAPPING IN PROGRESS

	// swap in the page
	read_page(swap_index, page->pa);

	// release coremap lock
	// sleep until swap in completes
	// lock coremap

	// mark page as DIRTY
	page->state = DIRTY;

	//release coremap lock

	return result;
}

/***********************************************************************************
************************************************************************************
************************************************************************************/

#else

static char swap_disk_file[] = "swapfile";

/* Initialization of the swap disk and the swap table. This one is for the swapfile version,
	the raw disk version is above. */
int swapspace_init(void)
{
	int result;

	// Open the hard drive for reading/writing.
	//result = vfs_open("lhd0raw:", O_RDWR, 0, &swapspace);
	//char open_disk[10];
	//strcpy(open_disk,swap_disk);

	result = vfs_open(swap_disk_file, O_RDWR|O_CREAT|O_TRUNC, 0, &swapspace);
	KASSERT(swapspace != NULL);

	// Make it known if all went well.
	if (result) {
		kprintf("Something has gone wrong with the swap disk: err %d\n",result);
	}
	else {
		kprintf("Opened %s as the swap disk.\n", swap_disk_file);
	}

	// Initialize the swap table to be empty; no pages in swap yet!
	// TODO: is the NULL init correct below?
	for (int i = 0; i < SWAP_MAX; i++) {
		//swap_table[i]->page = NULL;
		swap_table[i].as = NULL;
	}


	// Testing file disk writes
	// int buff1[1024];
	// int buff2[1024];
	// for(int i = 0; i < 1024; i++) {
	// 	buff1[i] = 3;
	// 	buff2[i] = 0;
	// }
	// struct iovec iov_write;
	// struct iovec iov_read;
	// struct uio page_write_uio;
	// struct uio page_read_uio;

	// off_t pos = 0x100; //SIND_TO_DISK(swap_index);

	// uio_kinit(&iov_write, &page_write_uio, buff1, PAGE_SIZE, pos, UIO_WRITE);
	// result = VOP_WRITE(swapspace, &page_write_uio);

	// int j = 0;
	// while(page_write_uio.uio_resid != 0) {
	// 	j++;
	// }

	// kprintf("j = %d\n", j);

	// pos = 0x100;

	// uio_kinit(&iov_read, &page_read_uio, buff2, PAGE_SIZE, pos, UIO_READ);
	// result = VOP_READ(swapspace, &page_read_uio);

	// int k = 0;
	// while(page_write_uio.uio_resid != 0) {
	// 	k++;
	// }

	// kprintf("k = %d\n", k);

	// kprintf("result buffer = %d\n", buff2[10]);


	return result;
}


/* SWAPFILE VERSION: Contains uio setup and execution for writting page to disk file. */
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

	// Need to wait for write to finish?

	return result;
}

/* SWAPFILE VERSION: Contains uio setup and execution for reading page from disk file. */
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

	// Need to wait for read to finish?

	return result;
}

/* SWAPFILE VERSION: Evict a CLEAN page from memory. Called by page swapping algorithm */

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
	struct tlbshootdown tlb;
	tlb.ts_vaddr = page->va;
	vm_tlbshootdown(&tlb);

	// Update the Page Table to list the page as swapped
	struct page_table *pt = pgdir_walk(page->as,page->va,false);
	int pt_index = VA_TO_PT_INDEX(page->va);
	pt->table[pt_index] |= PTE_SWAP;		 // Flips the bit to indicate swapped
	KASSERT(pt->table[pt_index] & PTE_SWAP); // Check that the bit was set correctly.

	// Update the coremap to list the physical page as free
	//free_kpages(page->va);

	vaddr_t page_location = PADDR_TO_KVADDR(page->pa);
	free_kpages(page_location);

	// Unlock core map
	//lock_release(core_map_lock);

	return result;

}

/* SWAPFILE VERSION: Swap the specified page out to disk; maked page clean but
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

	// Only handling singe pages right now
	KASSERT(page->npages == 1);

	// mark page as SWAPPING IN PROGRESS

	// Check swap space to see if this page is already there...
	swap_index = -1;	// -1 is an invalid index
	for (int i=0; i < SWAP_MAX; i++) {
		if (swap_table[i].as == page->as && swap_table[i].va == page->va) {
			swap_index = i;
			break;
		}
	}

	// ...if not, find free swap space.
	if (swap_index < 0) {
		for (int i=0; i < SWAP_MAX; i++) {
			if (swap_table[i].as == NULL) {
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

	// Lock the swap_table lock, since it's shared

	// Update swap bit map for new location as occupied
	// Update swap table so we can find this page later
	swap_table[swap_index].as = page->as;
	swap_table[swap_index].va = page->va;

	// Write page to disk
	write_page(swap_index, page->pa);

	// Unlock the swap_lock

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

/* SWAPFILE VERSION: Swap the specified page back into memory. */
int swapin_page(struct addrspace* as, vaddr_t va, struct page* page)
{

	// Check for free space in memory (finding the free space is the swap alg's job)
	KASSERT(page->as == NULL);
	KASSERT(as != NULL);

	volatile int result = 0;	// Incase we want to pass infor back up.
	int swap_index;



	// check coremap do i have?
	// lock cremap

	// double check that page exists
	// double check that page is swapped
	struct page_table *pt = pgdir_walk(as,va,false);
	int pt_index = VA_TO_PT_INDEX(va);
	KASSERT(pt->table[pt_index] & PTE_SWAP); // Check that the page is swapped.

	// lock the swap table

	// Locate the swapped page in the swap table
	swap_index = -1;	// -1 is an invalid index
	for (int i=0; i < SWAP_MAX; i++) {
		if (swap_table[i].as == as && swap_table[i].va == va) {
			swap_index = i;
			break;
		}
	}

	// Unlock swap table

	// ...if not, no swapped page exists...
	if(swap_index < 0) {
		panic("Tried to swap in a non-existant page.");
	}



		// (below will be job of swapping algorithm)
		//if space found, continue with swap in
		// if not free space:
			// Pick page to swap out
			//swapout_page()
			//evict()
	// write zeros to the evicted physical page
	// mark page as SWAPPING IN PROGRESS

	// swap in the page
	read_page(swap_index, page->pa);

	// release coremap lock
	// sleep until swap in completes
	// lock coremap

	// mark page as DIRTY
	page->state = DIRTY;

	//release coremap lock

	return result;
}


#endif

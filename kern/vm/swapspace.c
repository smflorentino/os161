
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
#include <cpu.h>
#include <addrspace.h>
#include <elf.h>
#include <spl.h>

/* Pick swapping implementation below; either swapfile or raw style. Comment out
	the one to not implement. */
#define SWAP_FILE	// Selects swapfile version of swapping
//#define SWAP_RAW	// Selects raw swap disk version of swapping

/* Pointer to swap hdd, lhd1raw: */
static struct vnode* swapspace;

// Array of swap entry pointers. Walk this to find your swapped page.
// Locate the page by comparing the page structure.
static struct swap_entry swap_table[SWAP_MAX];

static struct lock *swap_lock = NULL;						// Standard lock for swap
static struct spinlock swap_spinlock = SPINLOCK_INITIALIZER;	// Spinlock for swap


/* Routines to aquire and release the swap lock, same as how the coremap
	lock is aquired and released. */
bool
get_swap_lock(void)
{
	if(lock_do_i_hold(swap_lock))
	{
		return 0;
	}
	else
	{
		lock_acquire(swap_lock);
		return 1;
	}
}


void
release_swap_lock(bool release)
{
	if(release)
	{
		lock_release(swap_lock);
	}
}

/* Routines to aquire and release the swap spinlock, same as how the coremap
	lock is aquired and released. */
bool
get_swap_spinlock(void)
{
	if(spinlock_do_i_hold(&swap_spinlock))
	{
		return 0;
	}
	else
	{
		spinlock_acquire(&swap_spinlock);
		return 1;
	}
}


void
release_swap_spinlock(bool release)
{
	if(release)
	{
		spinlock_release(&swap_spinlock);
	}
}

/**********************************************************************************
***********************************************************************************
**********************************************************************************/

#ifdef SWAP_FILE

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

	// Need the swap lock from now on to protect the swap table.
	swap_lock = lock_create("swap_lock");
	spinlock_init(&swap_spinlock);

	kprintf("Swap Init Done.\n");

	// struct addrspace *as = as_create();
	// struct page *page = page_alloc(as,0x500000,PF_RW);

	// page->state = SWAPPING;

	// swapout_page(page);
	// evict_page(page);
	// swapin_page(as,0x500000,page);
	

	// struct page *page = page_alloc(0x0,0x0,0);
	// page->state = DIRTY;

	return result;
}


/* SWAPFILE VERSION: Contains uio setup and execution for writting page to disk file. */
static
int write_page(int swap_index, paddr_t page)
{
	//(void)swap_index;
	//(void)page;
	struct thread *thread = curthread;
	(void)thread;
	KASSERT(coremap_lock_do_i_hold());
	page = PADDR_TO_KVADDR(page);
	int result = 0;

	struct iovec iov;
	struct uio page_write_uio;

	off_t pos = SIND_TO_DISK(swap_index);

	uio_kinit(&iov, &page_write_uio, (void*)page, PAGE_SIZE, pos, UIO_WRITE);
	KASSERT(coremap_lock_do_i_hold());
	//bool lock = true;
	//release_coremap_lock(lock);
	result = VOP_WRITE(swapspace, &page_write_uio);

	//lock = get_coremap_lock();
	//KASSERT(coremap_lock_do_i_hold());
	// Need to wait for write to finish?

	return result;
}
/* SWAPFILE VERSION: Contains uio setup and execution for reading page from disk file. */
static
int read_page(int swap_index, paddr_t page)
{
	//(void)swap_index;
	//(void)page;
	struct thread *thread = curthread;
	(void)thread;
	// KASSERT(coremap_lock_do_i_hold());
	page = PADDR_TO_KVADDR(page);
	int result = 0;

	struct iovec iov;
	struct uio page_read_uio;

	off_t pos = SIND_TO_DISK(swap_index);

	uio_kinit(&iov, &page_read_uio, (void*)page, PAGE_SIZE, pos, UIO_READ);
	//bool lock = true;
	//release_coremap_lock(lock);
	result = VOP_READ(swapspace, &page_read_uio);
	//bool lock = get_coremap_lock();
	//(void)lock;
	//KASSERT(coremap_lock_do_i_hold());
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
	// bool lock = get_coremap_lock();
	// DEBUG(DB_SWAP,"Evicting PAGE %p\n", page);
	// Double check that the page exists
	// Double check that page is clean.
	KASSERT(page->state == CLEAN);
	KASSERT(page->as != NULL);
	KASSERT(page->va != 0x0);

	//ipi_broadcast(IPI_TLBSHOOTDOWN);

	// Update the Page Table to list the page as swapped
	struct page_table *pt = pgdir_walk(page->as,page->va,false);
	// DEBUG(DB_SWAP, "Evicting out VA:%p at Page: %d\n", (void*) page->va, page->pa / PAGE_SIZE);
	// DEBUG(DB_SWAP, "PT:%p\n", pt);
	int pt_index = VA_TO_PT_INDEX(page->va);
	// DEBUG(DB_SWAP, "PT Index: %d\n",pt_index);
	int* pte = &(pt->table[pt_index]);
	// DEBUG(DB_SWAP, "PTE:%p\n",(void*) *pte);
	KASSERT(PTE_TO_LOCATION(*pte) == PTE_SWAPPING); //Check we're evicting from memory
	// DEBUG(DB_SWAP, "PTE Location: %p\n", pte);
	// DEBUG(DB_SWAP, "PTE Before: %p\n", (void*) *pte);
	*pte &= 0xFFFFFFBF;		 // Flips the bit to indicate swapped
	pte = &(pt->table[pt_index]);	
	KASSERT(PTE_TO_LOCATION(*pte) == PTE_SWAP); // Check that the bit was set correctly.
	// DEBUG(DB_SWAP, "PTE After: %p\n", (void*) *pte);
	// Update the coremap to list the physical page as free
	//free_kpages(page->va);

	vaddr_t page_location = PADDR_TO_KVADDR(page->pa);
	free_kpages(page_location);
	KASSERT(page->state == FREE);
	// Unlock core map
	//lock_release(core_map_lock);
	// release_coremap_lock(lock);

	return result;

}

/* SWAPFILE VERSION: Swap the specified page out to disk; maked page clean but
	does NOT evict the page. */
int swapout_page(struct page* page)
{	
	// DEBUG(DB_SWAP,"SWO%d\n", page->pa/PAGE_SIZE);
	// bool lock = get_coremap_lock();
	// Shootdown the TLB for all CPU's
	// struct tlbshootdown tlb;
	// tlb.ts_vaddr = page->va;
	// ipi_tlbshootdown_broadcast(&tlb);
	KASSERT(coremap_lock_do_i_hold());
	// DEBUG(DB_SWAP,"O%p", page);
	KASSERT(page->state == SWAPPINGOUT);
	// DEBUG(DB_SWAP,"Swapping PAGE %p\n", page);
	int result = 0;
	int swap_index;
	//bool lock2 = get_swap_lock();
	KASSERT(page->as != NULL);

	// Update the Page Table to list the page as swapped
	struct page_table *pt = pgdir_walk(page->as,page->va,false);
	// DEBUG(DB_SWAP, "Evicting out VA:%p at Page: %d\n", (void*) page->va, page->pa / PAGE_SIZE);
	// DEBUG(DB_SWAP, "PT:%p\n", pt);
	int pt_index = VA_TO_PT_INDEX(page->va);
	// DEBUG(DB_SWAP, "PT Index: %d\n",pt_index);
	int* pte = &(pt->table[pt_index]);
	// DEBUG(DB_SWAP, "PTE:%p\n",(void*) *pte);
	KASSERT(PTE_TO_LOCATION(*pte) == PTE_SWAPPING); //Check we're evicting from memory
	// DEBUG(DB_SWAP, "Swapping out VA:%p at Page: %d\n", (void*) page->va, page->pa / PAGE_SIZE);

	// check coremap lock do i have?
	//lock coremap
	//bool holdlock = lock_do_i_hold(core_map_lock);
	//if(!holdlock) {
	//	lock_acquire(core_map_lock);
	//}

	// double check that page exists
	// double check that page is DIRTY
	// KASSERT(page->state == DIRTY);

	// Only handling singe pages right now
	KASSERT(page->npages == 1);

	// mark page as SWAPPING IN PROGRESS

	// Lock the swap table while checking and updating it.
	//bool sw_lock = get_swap_lock();
	bool sw_lock = get_swap_spinlock();

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

	//Swap table updated, so we can release the lock
	//release_swap_lock(sw_lock);
	release_swap_spinlock(sw_lock);

	// Write page to disk; page should be marked for swapping out
	KASSERT(page->state == SWAPPINGOUT);
	write_page(swap_index, page->pa);
	// Unlock the swap_lock

	// Unlock core map to sleep
	//lock_release(core_map_lock);

	// sleep until swap to disk completes, so that others can run
	// grab coremap lock

	// mark page as CLEAN; does not need a lock because the SWAPPING_OUT status 
	// protects the state of the page at this point
	KASSERT(page->state == SWAPPINGOUT);
	page->state = CLEAN;
	KASSERT(page->as != NULL);
	KASSERT(page->state == CLEAN);
	// KASSERT(coremap_lock_do_i_hold());
	// release coremap lock
	//release_swap_lock(lock2);
	// release_coremap_lock(lock);
	return result;
}

/* SWAPFILE VERSION: Swap the specified page back into memory. */
int swapin_page(struct addrspace* as, vaddr_t va, struct page* page)
{
	KASSERT(page->state == LOCKED);

	// KASSERT(coremap_lock_do_i_hold());
	// Check for free space in memory (finding the free space is the swap alg's job)
	// KASSERT(page->as == NULL);
	KASSERT(as != NULL);

	volatile int result = 0;	// Incase we want to pass infor back up.
	(void)result;
	int swap_index;



	// check coremap do i have?
	// bool lock = get_coremap_lock();
	//bool lock2 = get_swap_lock();

	// double check that page exists
	// double check that page is swappe
	// DEBUG(DB_SWAP, "SWIVA:%p\n", (void*) va);
	struct page_table *pt = pgdir_walk(as,va,false);
	// DEBUG(DB_SWAP, "PT:%p\n ", pt);
	int pt_index = VA_TO_PT_INDEX(va);
	// DEBUG(DB_SWAP, "PT Index: %d\n",pt_index);
	int pte = pt->table[pt_index];
	// DEBUG(DB_SWAP, "PTE to Swap in: %p\n", (void*) pte);
	// DEBUG(DB_SWAP, "PTE Location: %p\n", &(pt->table[pt_index]));
	KASSERT(PTE_TO_LOCATION(pte) == PTE_SWAP); // Check that the page is swapped.

	// lock the swap table
	//bool sw_lock = get_swap_lock();
	bool sw_lock = get_swap_spinlock();

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
	// Finished checking swap table, release its lock
	//release_swap_lock(sw_lock);
	release_swap_spinlock(sw_lock);

	// swap in the page
	read_page(swap_index, page->pa);

	// release coremap lock
	// sleep until swap in completes
	// lock coremap

	// mark page as DIRTY
	KASSERT(page->state == LOCKED);
	//int spl = splhigh();
	// page->state = DIRTY;
	page->va = va;
	// mark page as in memory TODO macro
	pt->table[pt_index] = PTE_IN_MEM(pt->table[pt_index]);
	//splx(spl);
	// KASSERT(page->state == DIRTY);
	KASSERT(PTE_TO_LOCATION(pt->table[pt_index]) == PTE_PM);
	//release coremap lock
	// release_swap_lock(lock2);
	// release_coremap_lock(lock);
	// KASSERT(coremap_lock_do_i_hold());
	return result;
}

/* Need to invalidate any swapped pages when as_destroy is called. */
int
clean_swapfile(struct addrspace* as, vaddr_t va)
{
	int result = 0;

	// Modifying the swap table, so lock it.
	//bool sw_lock = get_swap_lock();
	bool sw_lock = get_swap_spinlock();

	// Locate the swapped page in the swap table
	int swap_index = -1;	// -1 is an invalid index
	for (int i=0; i < SWAP_MAX; i++) {
		// AS and VA must both match to identify page.
		if (swap_table[i].as == as && swap_table[i].va == va) {
			swap_index = i;
			break;
		}
	}

	if (swap_index == -1) {
		panic("Tried to clean a swap page that doesn't exist.\n");
		//release_swap_lock(sw_lock);
		return result;
	}

	// All we do is make the swap entry null, same as in swapspace_init(). 
	DEBUG(DB_SWAP,"Cleaning a swap page.\n");
	swap_table[swap_index].as = NULL;

	//release_swap_lock(sw_lock);
	release_swap_spinlock(sw_lock);

	return result;
}

#endif

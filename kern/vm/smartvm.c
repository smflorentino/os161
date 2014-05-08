/*

	A Smart Virtual Memory System
*/

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <machine/vm.h>
#include <mainbus.h>
#include <spinlock.h>
#include <uio.h>
#include <kern/iovec.h>
#include <synch.h>
#include <addrspace.h>
#include <mips/tlb.h>
#include <current.h>
#include <spl.h>
#include <elf.h>
#include <swapspace.h>
/*
 * Wrap ram_stealmem in a spinlock.
 */
/* Maximum of 1MB of user stack */
#define VM_STACKPAGES	256
#define USER_STACK_LIMIT (0x80000000 - (VM_STACKPAGES * PAGE_SIZE)) 
#define SWAPPING_ENABLED

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static bool vm_initialized = false;

static struct page *core_map;
static size_t page_count;
/* Round-Robin TLB entry to sacrifice >:) */
static char tlb_offering = 0;
/* Round-Robin Page to sacrifice >:) */
static volatile short page_offering = 0;
//Some shenanigans here. This will be preserved throughout the life of the program. 
static volatile size_t current_index = 0;
static volatile size_t current_n_index = 0;
/* Number of free pages in memory  */
static size_t free_pages;
/* TODO figure out how to do this. I'll probably kmalloc it in
vm_bootstrap after we set the correct flag.*/ 
struct lock *core_map_lock = NULL;

/* Get the coremap lock, unless we already have it. 
 * It's like of modeled like the splhigh/splx methods.
 * Store the result of this method and pass it to 
 * release_coremap_lock when you're done */

bool
get_coremap_spinlock()
{
	if(spinlock_do_i_hold(&stealmem_lock))
	{
		return 0;
	}
	else
	{
		spinlock_acquire(&stealmem_lock);
		// KASSERT(spinlock_do_i_hold(&stealmem_lock));
		// DEBUG(DB_SWAP, "\n**GL**\n");
		return 1;
	}
}

/* Release the coremap lock, unless we still expect the lock
 * further up the stack. Modeled after splhigh/splx code,
 * see the comments on the above method as well.
 */
 
void
release_coremap_spinlock(bool release)
{
	if(release)
	{
		// DEBUG(DB_SWAP, "\n**RL**\n");
		spinlock_release(&stealmem_lock);
	}
}

bool
coremap_spinlock_do_i_hold()
{
	return spinlock_do_i_hold(&stealmem_lock);
}


bool
get_coremap_lock()
{
	if(lock_do_i_hold(core_map_lock))
	{
		return 0;
	}
	else
	{
		lock_acquire(core_map_lock);
		// KASSERT(spinlock_do_i_hold(&stealmem_lock));
		// DEBUG(DB_SWAP, "\n**GL**\n");
		return 1;
	}
}

/* Release the coremap lock, unless we still expect the lock
 * further up the stack. Modeled after splhigh/splx code,
 * see the comments on the above method as well.
 */
void
release_coremap_lock(bool release)
{
	if(release)
	{
		// DEBUG(DB_SWAP, "\n**RL**\n");
		lock_release(core_map_lock);
	}
}

bool
coremap_lock_do_i_hold()
{
	return lock_do_i_hold(core_map_lock);
}


/* Returns the next available index of a page we're going to page to disk. 
 * We do this round robin style at the moment, but I suppose we could change to
 * random at a later time, or even LRU with some tweaks to this method.
 */
static 
size_t
get_a_dirty_page_index(int retry)
{

	bool lock = get_coremap_lock();
	// KASSERT(spinlock_do_i_hold(&stealmem_lock));
	// DEBUG(DB_SWAP, "Cur:%d\n",current_index);
	size_t start = current_index;
	for(size_t i = start; i<page_count; i++)
	{
		// KASSERT(spinlock_do_i_hold(&stealmem_lock));
		// DEBUG(DB_SWAP,"DPI: %d\n", i);
		if(retry)
		{
			// DEBUG(DB_SWAP,"I: %d State: %d\n",i,core_map[i].state);
		}
		int spl = splhigh();
		if(core_map[i].state == DIRTY)
		{
			KASSERT(core_map[i].state == DIRTY);
			current_index = i+1;
			core_map[i].state = SWAPPINGOUT;
			//Update PTE to state PTE_SWAPPING
			struct page_table *pt = pgdir_walk(core_map[i].as,core_map[i].va,false);
			int pt_index = VA_TO_PT_INDEX(core_map[i].va);
			pt->table[pt_index] |= PTE_SWAPPING;
			// DEBUG(DB_SWAP, "%d\n",pt->table[pt_index]);
			//////////////////////////////////
			splx(spl);
			struct thread *thread = curthread;
			(void)thread;
			// DEBUG(DB_SWAP, "DPI: %d%d\n",i,core_map[i].state);
			// KASSERT(spinlock_do_i_hold(&stealmem_lock));
			KASSERT(core_map[i].state == SWAPPINGOUT);
			release_coremap_lock(lock);
			return i;
		}
		else
		{
			splx(spl);
		}
	}
	if(retry == 1)
	{
		int spl = splhigh();
		current_index = 0;
		size_t start = current_index;
		for(size_t i = start; i<page_count; i++)
		{
			// KASSERT(spinlock_do_i_hold(&stealmem_lock));
			// DEBUG(DB_SWAP,"DPI: %d\n", i);
			if(retry)
			{
				// DEBUG(DB_SWAP,"I: %d State: %d\n",i,core_map[i].state);
			}
			if(core_map[i].state == DIRTY)
			{
				KASSERT(core_map[i].state == DIRTY);
				current_index = i+1;
				core_map[i].state = SWAPPINGOUT;
				//Update PTE to state PTE_SWAPPING
				struct page_table *pt = pgdir_walk(core_map[i].as,core_map[i].va,false);
				int pt_index = VA_TO_PT_INDEX(core_map[i].va);
				pt->table[pt_index] |= PTE_SWAPPING;
				// DEBUG(DB_SWAP, "%d\n",pt->table[pt_index]);
				//////////////////////////////////
				splx(spl);
				struct thread *thread = curthread;
				(void)thread;
				// DEBUG(DB_SWAP, "DPI: %d%d\n",i,core_map[i].state);
				// KASSERT(spinlock_do_i_hold(&stealmem_lock));
				KASSERT(core_map[i].state == SWAPPINGOUT);
				release_coremap_lock(lock);
				return i;
			}
		}
		//splx(spl);
		retry = 2;
		// panic("get a dirty page index failed");	
	}
	if(retry == 2)
	{
		DEBUG(DB_SWAP,"\n");
		for(size_t i = 0;i<page_count;i++)
		{
			//int spl = splhigh();
			DEBUG(DB_SWAP,"I:%d S:%d\n",i,core_map[i].state);
			//splx(spl);
		}
		
		panic("get a dirty page index failed");

		// DEBUG(DB_SWAP,"retry\n");
		return -1;
		// panic("Couldn't find a dirty page!");
	}
	//Reached end of core map. Start from beginning.
	current_index = 0;
	// release_coremap_lock(lock);
	// DEBUG(DB_SWAP,"??%d\n",spinlock_do_i_hold(&stealmem_lock));
	// KASSERT(spinlock_do_i_hold(&stealmem_lock) == true);
	return get_a_dirty_page_index(1);


	panic("Get dirty page index is broken!");
	// return -1;
}


 #ifdef SWAPPING_ENABLED

/* Called in page_alloc ONLY at the moment.
 * This method will page available IF NEEDED - i.e. if there are less than 10 free
 * pages on the system, we'll start swapping. If not, we simply return.
 */
static
void
make_page_available()
{
	//KASSERT(spinlock_do_i_hold(&stealmem_lock));
	// DEBUG(DB_SWAP, "Free Pages: %d\n",free_pages);	
	if(free_pages <= 10) {
		struct thread *cur = curthread;
		(void)cur;
		// DEBUG(DB_SWAP, "Making page available\n");
		//DEBUG(DB_SWAP, "Start Swap: %d\n",free_pages);	
		//DEBUG(DB_SWAP, "\nStart Swapping\n");	
		int rr_page = get_a_dirty_page_index(0);
		if(rr_page == -1)
		{
			return;
		}
		//KASSERT(spinlock_do_i_hold(&stealmem_lock));
		KASSERT(core_map[rr_page].state == SWAPPINGOUT);
		// DEBUG(DB_SWAP, "SWOs%d\n",rr_page);
		swapout_page(&core_map[rr_page]);
		// KASSERT(spinlock_do_i_hold(&stealmem_lock));
		// DEBUG(DB_SWAP, "Starting Eviction...%d\n",rr_page);
		evict_page(&core_map[rr_page]);
		// KASSERT(spinlock_do_i_hold(&stealmem_lock));
		//DEBUG(DB_SWAP, "Evicted %d\n",rr_page);
	}
	// KASSERT(spinlock_do_i_hold(&stealmem_lock));
}

/* Called in page_nalloc ONLY at the moment.
 * This method will page available IF NEEDED - i.e. if there are less than 10 free
 * pages on the system, we'll start swapping. If not, we simply return. 
 */
static
void
make_pages_available(int npages, bool retry)
{
	/*
	//KASSERT(spinlock_do_i_hold(&stealmem_lock));
	// bool lock = get_coremap_lock();
	// DEBUG(DB_SWAP,"MPA:%d\n",npages);
	if(free_pages <= 10) {
		//Disable interrupts until we find the right number of pages. ->should be disabled in page_alloc()
		//int spl = splhigh();
		size_t rr_page = current_n_index;
		if(retry)
		{
			rr_page = 0;
		}
		bool blockStarted = false;
		int pagesFound = 0;
		int startingPage = 0;
		// DEBUG(DB_SWAP, "NPages:%d\n",npages);
		// DEBUG(DB_SWAP, "Page Count:%d\n",page_count);
		for(size_t i = rr_page;i < (size_t) page_count;i++)
		{
			// DEBUG(DB_SWAP, "Page: %d State: %d\n",i,core_map[i].state);
			page_state_t curState = core_map[i].state;
			if(!blockStarted && (curState == DIRTY || curState == FREE))
			{
				blockStarted = true;
				pagesFound = 1;
				startingPage = i;
			}
			else if(blockStarted && (curState != DIRTY && curState != FREE))
			{
				blockStarted = false;
				pagesFound = 0;
			}
			else if(blockStarted && (curState == DIRTY || curState == FREE))
			{
				pagesFound++;
			}
			if(pagesFound == npages)
			{
				//Mark the pages as SWAPPING OUT.
				for(int j = startingPage; j<startingPage + npages; j++)
				{
					KASSERT(core_map[j].state == DIRTY || core_map[j].state == FREE);
					if(core_map[j].state == DIRTY)
					{
						core_map[j].state = SWAPPINGOUT;
						//Update PTE to state PTE_SWAPPING
						struct page_table *pt = pgdir_walk(core_map[j].as,core_map[j].va,false);
						int pt_index = VA_TO_PT_INDEX(core_map[j].va);
						pt->table[pt_index] |= PTE_SWAPPING;
						// DEBUG(DB_SWAP, "%d\n",pt->table[pt_index]);
						//////////////////////////////////
					}
				}
				current_index = startingPage + npages + 1;
				//splx(spl);
				//Swap out the block of pages, now
				for(int j = startingPage; j<startingPage + npages; j++)
				{
					KASSERT(core_map[j].state == FREE || core_map[j].state == SWAPPINGOUT);
					// DEBUG(DB_SWAP,"SWOn%d-%d\n",j,npages);
					if(core_map[j].state == SWAPPINGOUT)
					{
						//KASSERT(spinlock_do_i_hold(&stealmem_lock));
						swapout_page(&core_map[j]);
						evict_page(&core_map[j]);	
					}
				}
				// release_coremap_lock(lock);
				return;
			}
		}
		//If we get here, we reached the end of memory. Try again ONCE.
		if(retry == 2)
		{
			DEBUG(DB_SWAP,"\n");
			for(size_t i = 0;i<page_count;i++)
			{
				//int spl = splhigh();
				DEBUG(DB_SWAP,"I:%d S:%d\n",i,core_map[i].state);
				//splx(spl);
			}

			panic("Couldn't swap a big enough chunk for npages!");
		}
		else
		{
			//splx(spl);
			make_pages_available(npages,1);
		}
	}
	*/
	// release_coremap_lock(lock);


	(void)npages;
	(void)retry;
	// Since we probably need a lot of pages, due to forking and such,
	// why not just flush all of memory?
	if(free_pages > 10)
	{
		return;
	}
	bool lock; 
	lock = get_coremap_lock();

	for (int i = 0; i < (int)page_count; i++) {
		int spl = splhigh();
		if(core_map[i].state == DIRTY) {
			core_map[i].state = SWAPPINGOUT;
			splx(spl);
			//Update PTE to state PTE_SWAPPING
			struct page_table *pt = pgdir_walk(core_map[i].as,core_map[i].va,false);
			int pt_index = VA_TO_PT_INDEX(core_map[i].va);
			pt->table[pt_index] |= PTE_SWAPPING;
			swapout_page(&core_map[i]);
			evict_page(&core_map[i]);	
		}
		else
		{
			splx(spl);
		}
	}

	release_coremap_lock(lock);
}
#endif

/* Initialization function */
void vm_bootstrap() 
{
	
	/* Get the firstaddr and lastaddr of physical memory.
	It will most definitely be less than actual memory; becuase
	before the VM bootstraps we have to use getppages (which in turn
	calls stealmem).
	*/
	paddr_t firstaddr, lastaddr;
	ram_getsize(&firstaddr,&lastaddr);
	/* The number of pages (core map entries) will be the size of 
	physical memory (lastaddr *should* not change) divided by PAGE_SIZE
	*/
	page_count = lastaddr / PAGE_SIZE;
	/* Allocate space for the coremap *without* using kmalloc. This 
	solves the chicken-and-egg problem. Simply set the core_map pointer
	to point to the first available address of memory as of this point;
	then increment freeaddr by the size of the coremap (we're effectively 
	replicating stealmem here, but we're not grabbing a whole page) */
	core_map = (struct page*)PADDR_TO_KVADDR(firstaddr);
	paddr_t freeaddr = firstaddr + page_count * sizeof(struct page);
	// kprintf("FirstAddr: %p\n", (void*) firstaddr);
	// kprintf("Freeaddr: %p\n", (void*) freeaddr);
	// kprintf("Size of Core Map: %d\n", page_count * sizeof(struct page));
	// kprintf("Base Addr of core_map: %p\n", &core_map);
	// kprintf("Base Addr of core_map[0]: %p\n", &core_map[0]);
	// kprintf("Base Addr of core_map[127]: %p\n", &core_map[127]);
	
	/* Calculate the number of fixed pages. The number of fixed pages
	will be everything from 0x0 to freeaddr (essentially all the stolen
	memory will be FIXED). This might be a signficant amount; up until VM bootstrapping
	kmalloc will steal memory. This is the only way to solve the chicken-and-egg problem
	of the VM needing kmalloc and kmalloc needing VM.*/
	size_t num_of_fixed_pages = (freeaddr / PAGE_SIZE);
	if(freeaddr % PAGE_SIZE != 0)
	{
		/*If the stolen memory crosses a page boundry (probabably almost always will)
		mark that partially stolen page as FIXED*/
		num_of_fixed_pages++;
	}
	//Now, mark every (stolen) page from 0 to freeaddr as FIXED...
	for(size_t i = 0; i<num_of_fixed_pages; i++)
	{
		// kprintf("Address of Core Map %d: %p ",i,&core_map[i]);
		// kprintf("PA of Core Map %d:%p\n", i, (void*) (PAGE_SIZE * i));
		core_map[i].pa = i * PAGE_SIZE;
		// core_map[i].va = PADDR_TO_KVADDR(i * PAGE_SIZE);
		core_map[i].state = FIXED;
		core_map[i].as = 0x0;
	}
	/* Mark every available page (from freeaddr + offset into next page,
	if applicable) to lastaddr as FREE*/
	for(size_t i = num_of_fixed_pages; i<page_count; i++)
	{
		// kprintf("Address of Core Map %d: %p\n",i,&core_map[i]);
		// kprintf("PA of Core Map %d:%p\n", i, (void*) (PAGE_SIZE * i));
		// kprintf("KVA of Core Map %d:%p\n",i, (void*) PADDR_TO_KVADDR(PAGE_SIZE*i));
		core_map[i].state = FREE;
		core_map[i].as = 0x0;
		core_map[i].va = 0x0;
		free_pages++;
	}
	/* Set VM initialization flag. alloc_kpages and free_kpages
	should behave accordingly now*/
	vm_initialized = true;
	/* Now that the VM is initialized, create a lock */
	spinlock_cleanup(&stealmem_lock);
	spinlock_init(&stealmem_lock);
	core_map_lock = lock_create("coremap_lock");
}

/* Fault handling function called by trap code */

int vm_fault(int faulttype, vaddr_t faultaddress) 
{
	bool lock = false;	// Indicates if lock was aquired in "this" function
	//int spl = splhigh();
	// bool lock = get_coremap_lock();
	// DEBUG(DB_VM,"F:%p\n",(void*) faultaddress);
	struct addrspace *as = curthread->t_addrspace;
	//We ALWAYS update TLB with writable bits ASAP. So this means a fault.
	if(faulttype == VM_FAULT_READONLY && as->use_permissions)
	{
		// DEBUG(DB_VM, "NOT ALLOWED\n");
		//splx(spl);
		return EFAULT;
	}
	//Null Pointer
	if(faultaddress == 0x0)
	{	
		//splx(spl);
		return EFAULT;
	}
	//Align the fault address to a page (4k) boundary.
	faultaddress &= PAGE_FRAME;
	
	//Make sure address is valid
	if(faultaddress >= 0x80000000)
	{
		//splx(spl);
		return EFAULT;
	}
	/*If we're trying to access a region after the end of the heap but 
	 * before the stack, that's invalid (unless load_elf is running) */
	if(as->loadelf_done && faultaddress < USER_STACK_LIMIT && faultaddress > as->heap_end)
	{
		//splx(spl);
		return EFAULT;
	}
	
	//Translate....
	struct page_table *pt = pgdir_walk(as,faultaddress,false);
	int pt_index = VA_TO_PT_INDEX(faultaddress);
	int pfn = PTE_TO_PFN(pt->table[pt_index]);
	int permissions = PTE_TO_PERMISSIONS(pt->table[pt_index]);
	int swapped = PTE_TO_LOCATION(pt->table[pt_index]);
	struct page *page = NULL;

	/*If the PFN is 0, we might need to dynamically allocate
	on the stack or the heap */
	if(pfn == 0)
	{
		//Stack
		if(faultaddress < as->stack && faultaddress > USER_STACK_LIMIT)
		{
			as->stack -= PAGE_SIZE;
			lock = get_coremap_lock();
			page = page_alloc(as,as->stack, PF_RW);
			release_coremap_lock(lock);
		}
		//Heap
		else if(faultaddress < as->heap_end && faultaddress >= as->heap_start)
		{
			lock = get_coremap_lock();
			page = page_alloc(as,faultaddress, PF_RW);
			release_coremap_lock(lock);
		}
		//Static Segment(s)
		else if(faultaddress < as->heap_start && faultaddress >= as->static_start)
		{
			panic("code not loaded: %p",(void*) faultaddress);
			//TODO
			// page = page_alloc(as,faultaddress,PF_)
		}
		else
		{
			//splx(spl);
			return EFAULT;
		}
	}

	/*We grew the stack and/or heap dynamically. Try translating again */
	pt = pgdir_walk(as,faultaddress,false);
	pt_index = VA_TO_PT_INDEX(faultaddress);
	pfn = PTE_TO_PFN(pt->table[pt_index]);
	permissions = PTE_TO_PERMISSIONS(pt->table[pt_index]);
	swapped = PTE_TO_LOCATION(pt->table[pt_index]);

	/* If we're swapped out, time to do some extra stuff. */
	while(swapped == PTE_SWAPPING)
	{
		// Busy wait for the swap to complete, since we cannot sleep in an interrupt
		thread_yield();
		pfn = PTE_TO_PFN(pt->table[pt_index]);
		permissions = PTE_TO_PERMISSIONS(pt->table[pt_index]);
		swapped = PTE_TO_LOCATION(pt->table[pt_index]);
	}

	// Swap completed and page is now in memory or on disk; if disk, bring it back to memory
	if(swapped == PTE_SWAP)
	{
		//bool lock = get_coremap_lock();
		//TODO get the page back in to ram. 
		//Does this work?
		// DEBUG(DB_SWAP,"PTE (vmfault)1:%p\n",(void*) pt->table[pt_index]);
		lock = get_coremap_lock();

		page = page_alloc(as,faultaddress,permissions);
		/* Page now has a home in RAM. But set the swap bit to 1 so we can swap the page in*/
		pt->table[pt_index] |= PTE_SWAP;
		// DEBUG(DB_SWAP,"PTE (vmfault)2:%p\n",(void*) pt->table[pt_index]);
		swapin_page(as,faultaddress,page);

		release_coremap_lock(lock);

		/* Page was swapped back in. Re-translate */
		pt = pgdir_walk(as,faultaddress,false);
		pt_index = VA_TO_PT_INDEX(faultaddress);
		pfn = PTE_TO_PFN(pt->table[pt_index]);
		permissions = PTE_TO_PERMISSIONS(pt->table[pt_index]);
		swapped = PTE_TO_LOCATION(pt->table[pt_index]);
		//release_coremap_lock(lock);
	}

	// DEBUG(DB_VM, "PTERWX:%d\n",permissions);
	//Page is writable if permissions say so or if we're ignoring permissions.
	bool writable = (permissions & PF_W) || !(as->use_permissions);

	//This time, it shouldn't be 0.
	//Static Segment(s)
	// if(faultaddress < as->heap_start && faultaddress >= as->static_start)
	// {
	// 	panic("code not loaded: %p",(void*) faultaddress);
	// 	//TODO
	// 	// page = page_alloc(as,faultaddress,PF_)
	// }
	KASSERT(pfn > 0);
	KASSERT(pfn <= PAGE_SIZE * (int) page_count);

	uint32_t ehi,elo;

	/* Disable interrupts on this CPU while frobbing the TLB. */

	lock = get_coremap_spinlock();
	int spl = splhigh();

	// What does it mean for the page to be NULL in this case?
	if(page != NULL)
	{
		DEBUG(DB_VM, "Page State: %d\n", page->state);
		page->state = DIRTY;
	}

	for (int i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);

		if (elo & TLBLO_VALID) {
			// kprintf("Index %d in use\n",i);
			continue;
		}

		ehi = faultaddress;
		elo = pfn | TLBLO_VALID;

		if(writable)
		{
			elo |= TLBLO_DIRTY;
		}

		// kprintf("Writing TLB Index %d\n",i); 
		// DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, pfn);
		tlb_write(ehi, elo, i);

		splx(spl);
		release_coremap_spinlock(lock);
		return 0;
	}

	/*If we get here, TLB was full. Kill an entry, round robin style*/
	ehi = faultaddress;
	elo = pfn | TLBLO_VALID;
	if(writable)
	{
		elo |= TLBLO_DIRTY;
	}
	tlb_write(ehi,elo,tlb_offering);
	tlb_offering++;
	if(tlb_offering == NUM_TLB)
	{
		//At the end of the TLB. Start back at 0 again.
		tlb_offering = 0;
	}

	splx(spl);
	release_coremap_spinlock(lock);
	return 0;
}

/* Given a virtual address & an address space, return the page table from the page directory */
struct page_table *
pgdir_walk(struct addrspace *as, vaddr_t va, bool create)
{
	struct page_table* pt;
	//Get the top 10 bits from the virtual address to index the PD.
	int pd_index = VA_TO_PD_INDEX(va);
	//Get the page table in the page directory, using index we just made
	pt = as->page_dir[pd_index];
	if(pt == NULL && create)
	{
		//Store the location of the page table in the page directory;
		pt = kmalloc(sizeof(struct page_table));
		for(size_t i =0;i<1024;i++)
		{
			pt->table[i] = 0;
		}
		as->page_dir[pd_index] = pt;
	}
	// KASSERT(pt != NULL);
	return pt;
}

/* Given a page table entry, return the page.
 * We pass in the page directory index and page table index 
 * so we can re-create the virtual address if we need to.
 *
 * If the page was swapped out on disk, swap it in before returning. */
struct page *
get_page(int pdi, int pti, struct page_table* pt)
{
	//bool lock_p = false;
	int swapped = PTE_TO_LOCATION(pt->table[pti]);
	
	// Wait for page to finish moving to disk, or moving to memory.
	while(swapped == PTE_SWAPPING)
	{
		// Let other stuff run so that this can complete
		/*
		if(spinlock_do_i_hold(&stealmem_lock)){
			spinlock_release(&stealmem_lock);
			//lock_p = true;
		}
		*/
		thread_yield();
		swapped = PTE_TO_LOCATION(pt->table[pti]);
		/*
		// Get the lock back if we had released it above.
		if(!spinlock_do_i_hold(&stealmem_lock)){
			spinlock_acquire(&stealmem_lock);
		}
		*/
		//DEBUG(DB_SWAP,"status: %d", swapped);
	}
	
	//if(swapped == PTE_SWAPPING)
	//{
	//	panic("BOB SAGGOT");
	//}

	int* pte = &(pt->table[pti]);
	if(swapped == PTE_SWAP)
	{
		//bool lock = get_coremap_lock();
		//Get the address space, virtual address, and permissions from PTE.
		struct addrspace *as = curthread->t_addrspace;
		vaddr_t va = PD_INDEX_TO_VA(pdi) | PT_INDEX_TO_VA(pti);
		int permissions = PTE_TO_PERMISSIONS(*pte);
		//KASSERT(coremap_lock_do_i_hold());
		//Allocate a page
		struct page *page = page_alloc(as,va,permissions);
		struct page_table *pt = pgdir_walk(as,va,false);
		KASSERT(page->state == LOCKED);
		/* Page now has a home in RAM. But set the swap bit to 1 so we can swap the page in*/
		pt->table[pti] |= PTE_SWAP;
		//Swap the page in
	
		swapin_page(as,va,page);
		/* Page was swapped back in. Re-translate */
		pt = pgdir_walk(as,va,false);
		*pte = pt->table[pti];
		// DEBUG(DB_SWAP,"I%p\n", page);
		//release_coremap_lock(lock);
	}
	int page_num = PTE_TO_PFN(*pte) / PAGE_SIZE;
	return &core_map[page_num];
}

/* Copy the contents of a page */
void
copy_page(struct page *src, struct page *dst)
{
	memcpy(dst,src,sizeof(struct page));
}

/* Allocate pages BEFORE the VM is bootstrapped.
 * Steals memory.
 */
static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	
	spinlock_release(&stealmem_lock);
	return addr;
}

/* Shamelessly stolen from memset.c */
static
void
memset(void *ptr, int ch, size_t len)
{
	char *p = ptr;
	size_t i;
	
	for (i=0; i<len; i++) {
		p[i] = ch;
	}
}

/* Zero a page. Called during allocation */
/* Calls function above. Might need to change later,
but works for KVAs at the time being 
//TODO use UIO word-aligned setting
for better performance?? */
static
void
zero_page(size_t page_num)
{
	vaddr_t ptr = PADDR_TO_KVADDR(core_map[page_num].pa);

	memset((void*) ptr,'\0',PAGE_SIZE);
}

/* Allocate a page for use by the kernel */
static
void 
allocate_fixed_page(size_t page_num)
{
	// KASSERT(spinlock_do_i_hold(&stealmem_lock));
	KASSERT(core_map[page_num].state == FREE);
	core_map[page_num].state = FIXED;
	paddr_t pa = page_num * PAGE_SIZE;
	core_map[page_num].pa = pa;
	core_map[page_num].va = 0x0;
	core_map[page_num].as = NULL;
	zero_page(page_num);
	free_pages--;
	// DEBUG(DB_VM, "AF:%d\n",free_pages);
}

/* Allocate a page in a user address space */
static
void
allocate_nonfixed_page(size_t page_num, struct addrspace *as, vaddr_t va, int permissions)
{
	// KASSERT(spinlock_do_i_hold(&stealmem_lock));
	KASSERT(core_map[page_num].state == FREE);
	//Allocate a page
	core_map[page_num].state = LOCKED;
	paddr_t pa = page_num * PAGE_SIZE;
	core_map[page_num].pa = pa;
	core_map[page_num].va = va;
	core_map[page_num].as = as;

	//Get the page table for the virtual address.
	struct page_table *pt = pgdir_walk(as,va,true);

	KASSERT(pt != NULL);
	KASSERT(pt != 0x0);

	//Update the page table entry to point to the page we made.
	size_t pt_index = VA_TO_PT_INDEX(va);
	vaddr_t page_location = PADDR_TO_KVADDR(core_map[page_num].pa);
	pt->table[pt_index] = PAGEVA_TO_PTE(page_location);
	// DEBUG(DB_VM, "VA:%p\n", (void*) va);
	// DEBUG(DB_VM, "PTE:%p\n", (void*) pt->table[pt_index]);
	// DEBUG(DB_VM, "PFN:%p\n", (void*) PTE_TO_PFN(pt->table[pt_index]));
	//Add in permissions
	pt->table[pt_index] |= permissions;

	zero_page(page_num);
	free_pages--;
	// DEBUG(DB_VM, "A:%d\n",free_pages);
}

/* Pre-Allocate a Page in User Address Space. Simply Set Permissions*/
static
void
pre_allocate_nonfixed_page(struct addrspace *as,vaddr_t va,int permissions)
{
	//Get the page table in the page directory (create one, if needed)
	struct page_table *pt = pgdir_walk(as,va,true);
	//Get the index into the page table
	size_t pt_index = VA_TO_PT_INDEX(va);
	//Set the permissions (used later when we actually allocate)
	pt->table[pt_index] |= permissions;
}

/* Called by free_kpages */
static
void 
free_fixed_page(size_t page_num)
{
	// KASSERT(lock_do_i_hold(core_map_lock));
	core_map[page_num].va = 0x0;
	core_map[page_num].as =  NULL;
	core_map[page_num].npages = 0;
	core_map[page_num].state = FREE;
	free_pages++;

	// DEBUG(DB_VM, "FR:%d\n",free_pages);
	// if(core_map[page_num].as != NULL)
	// {
	// 	panic("I don't know how to free page with an address space");
	// }
}

/* Allocate a page. Either kernel or user. */
struct page *
page_alloc(struct addrspace* as, vaddr_t va, int permissions)
{
	//int spl = splhigh();
	//bool lock = get_coremap_lock();

	//KASSERT(spinlock_do_i_hold(&stealmem_lock));
	// DEBUG(DB_SWAP, "Need page for %p\n",(void*) va);
	#ifdef SWAPPING_ENABLED
	//Make a page available for allocation, if needed.
	make_page_available();
	// KASSERT(spinlock_do_i_hold(&stealmem_lock));
	#endif

	// Loop through the coremap 7 times, trying to find a free page.
	int j = 0;
	while(j < 7){
		for(size_t i = 0;i<page_count;i++)
		{
			//int spl = splhigh();
			if(core_map[i].state == FREE)
			{
				if(as == NULL)
				{
					KASSERT(va == 0x0);
					// DEBUG(DB_SWAP, "Getting page %d for FIXED\n",i);
					allocate_fixed_page(i);				
				}
				else
				{
					KASSERT(va != 0x0);
					// DEBUG(DB_SWAP, "Getting page %d for %p\n",i, (void*) va);
					allocate_nonfixed_page(i,as,va,permissions);
				}
				core_map[i].npages = 1;
				//release_coremap_lock(lock);
				//splx(spl);
				return &core_map[i];
			}
		}

		#ifdef SWAPPING_ENABLED
		//Make a page available for allocation, if needed.
		make_page_available();
		// KASSERT(spinlock_do_i_hold(&stealmem_lock));
		#endif

		j++;
	}

	// Couldn't find a free page after 7 loops, something is wrong.
	DEBUG(DB_SWAP,"\n\nFailed in page_alloc()\n");
	for(size_t i = 0;i<page_count;i++)
	{
		DEBUG(DB_SWAP,"%d: %d\n",i,core_map[i].state);
	}
	//release_coremap_lock(lock);
	//splx(spl);

	panic("No available pages for single page alloc!");
	return 0x0;
}

/* Pre-Allocate page in user address space*/
void
page_prealloc(struct addrspace *as, vaddr_t va, int permissions)
{
	pre_allocate_nonfixed_page(as,va,permissions);
}




/* Called by alloc_kpages */
static
vaddr_t
page_nalloc(int npages)
{
	bool lock = get_coremap_lock();
	//KASSERT(spinlock_do_i_hold(&stealmem_lock));
	bool blockStarted = false;
	int pagesFound = 0;
	int startingPage = 0;

	#ifdef SWAPPING_ENABLED
	//Make a page available for allocation, if needed.
	make_pages_available(npages,false);
	#endif

	int spl = splhigh();
	for(size_t i = 0;i<page_count;i++)
	{
		if(!blockStarted && core_map[i].state == FREE)
		{
			blockStarted = true;
			pagesFound = 1;
			startingPage = i;
		}
		else if(blockStarted && core_map[i].state != FREE)
		{
			blockStarted = false;
			pagesFound = 0;
		}
		else if(blockStarted && core_map[i].state == FREE)
		{
			pagesFound++;
		}
		if(pagesFound == npages)
		{
			// DEBUG(DB_SWAP, "Getting %d npages %d-%d for FIXED\n",npages,startingPage,startingPage+npages-1);
			//KASSERT(spinlock_do_i_hold(&stealmem_lock));
			//Allocate the block of pages, now.
			for(int j = startingPage; j<startingPage + npages; j++)
			{
				allocate_fixed_page(j);
			}
			core_map[startingPage].npages = npages;
			release_coremap_lock(lock);
			splx(spl);
			return PADDR_TO_KVADDR(core_map[startingPage].pa);
		}
	}
	panic("Couldn't find a big enough chunk for npages!");
	return 0x0;	
}

/* Allocate kernel heap pages (called by kmalloc) */
vaddr_t alloc_kpages(int npages)
{
	if(!vm_initialized) {
		/* Use dumbvm if VM not initialized yet */
		paddr_t pa;
		pa = getppages(npages);
		if (pa==0) {
			return 0;
		}
		return PADDR_TO_KVADDR(pa);
	}
	if(npages == 1) {
		struct page *kern_page = page_alloc(0x0,0x0,0);
		return PADDR_TO_KVADDR(kern_page->pa);
	}
	else if(npages > 1) {
		return page_nalloc(npages);
	}
	else {
		panic("alloc_kpages called with negiatve page count!");
	}
	vaddr_t t;
	return t;
}

/* Free a page, either user or kernel. */
void free_kpages(vaddr_t addr)
{
	if(addr < 0x80000000)
	{
		panic("Tried to free a direct-mapped address\n");	
	}
	
	// bool lock = get_coremap_lock();
	/* Disable interrupts on this CPU while frobbing the TLB. */
	//int spl = splhigh();
	//bool lock = get_coremap_lock();

	// kprintf("Freeing VA:%p\n", (void*) addr);
	KASSERT(page_count > 0);
	for(size_t i = 0;i<page_count;i++)
	{
		vaddr_t page_location = PADDR_TO_KVADDR(core_map[i].pa);
		// DEBUG(DB_VM, "Page locaion:%p\n", (void*) page_location);
		if(addr == page_location)
		{
			// size_t target = i + core_map[i].npages;
			for(size_t j = i; j<i+core_map[i].npages;j++)
			{
				// DEBUG(DB_SWAP, "FREE %p\n",&core_map[j]);
				free_fixed_page(j);
			}
			// release_coremap_lock(lock);
			//release_coremap_lock(lock);
			//splx(spl);
			return;
		}
	}
	panic("VA Doesn't exist!");
}

void unlock_loading_pages(struct addrspace *as)
{
	for(size_t i = 0;i<page_count;i++)
	{
		if(core_map[i].as == as)
		{
			core_map[i].state = DIRTY;
		}
	}
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void)
{
	int spl;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	bool lock = get_coremap_spinlock();
	spl = splhigh();

	/* Shoot down all the TLB entries. */
	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
	release_coremap_spinlock(lock);

	return;
}

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	//(void)ts;

	int tlb_entry, spl;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	bool lock = get_coremap_spinlock();
	spl = splhigh();
	// Probe TLB so see if particular VA is present.
	tlb_entry = tlb_probe(VA_TO_VPF(ts->ts_vaddr), 0);
	if(tlb_entry < 0) {
		// No entry found, so shoot down succeeded
		splx(spl);
		return;
	}

	// Invalidate the particular TLB entry
	tlb_write(TLBHI_INVALID(tlb_entry), TLBLO_INVALID(), tlb_entry);

	splx(spl);
	release_coremap_spinlock(lock);

}

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
/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static bool vm_initialized = false;

static struct page *core_map;
static size_t page_count;
/* TODO figure out how to do this. I'll probably kmalloc it in
vm_bootstrap after we set the correct flag.*/ 
static struct lock *core_map_lock = NULL;

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
	}
	/* Set VM initialization flag. alloc_kpages and free_kpages
	should behave accordingly now*/
	vm_initialized = true;
	/* Now that the VM is initialized, create a lock */
	core_map_lock = lock_create("coremap lock");
}

/* Fault handling function called by trap code */
//TODO permissions.
int vm_fault(int faulttype, vaddr_t faultaddress) 
{
	if(faultaddress == 0x0)
	{
		return EFAULT;
	}
	faultaddress &= PAGE_FRAME;
	struct addrspace *as = curthread->t_addrspace;
	// DEBUG(DB_VM, "VA:%p\n", (void*) faultaddress);
	// DEBUG(DB_VM, "St:%p\n", (void*) as->stack);
	//Make sure address is valid
	if(faultaddress >= 0x80000000)
	{
		return EFAULT;
	}
	if(faultaddress > as->heap_end && faultaddress < as->stack)
	{
		return EFAULT;
	}
	(void)faulttype;

	//If we're coming down the stack....
	if(faultaddress < as->stack && faultaddress > as->heap_end)
	{
		as->stack -= PAGE_SIZE;
		page_alloc(as,as->stack);
		// struct page_table *pt = pgdir_walk(as,as->stack,true);
		// vaddr_t stack_page_va = page_alloc(as);
		// //Update the page table entry to point to the page we made.
		// size_t pt_index = VA_TO_PT_INDEX(as->stack);
		// pt->table[pt_index] = PAGEVA_TO_PTE(stack_page_va);
	}
	
	//Translate....
	struct page_table *pt = pgdir_walk(as,faultaddress,false);
	int pt_index = VA_TO_PT_INDEX(faultaddress);
	int pfn = PTE_TO_PFN(pt->table[pt_index]);

	//If PFN is 0, and we're inside the heap, could be that page
	//was not allocated yet. Try to allocate it now.
	if(pfn == 0 && faultaddress < as->stack && faultaddress >= as->heap_start)
	{
		page_alloc(as,faultaddress);
	}
	else if(pfn == 0)
	{
		//Invalid address
		return EFAULT;
	}
	KASSERT(pfn >= 0);

	uint32_t ehi,elo;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			// kprintf("Index %d in use\n",i);
			continue;
		}
		ehi = faultaddress;
		elo = pfn | TLBLO_DIRTY | TLBLO_VALID;
		// kprintf("Writing TLB Index %d\n",i); 
		// DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, pfn);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;


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
		as->page_dir[pd_index] = pt;
	}
	return pt;
}

/* Given a page table entry, return the page*/
//TOOO check if page is swapped in or not
//if its on disk, get it.
struct page *
get_page(int pte)
{
	//PTE will be top 20 bits of physical address. .
	pte = pte / PAGE_SIZE;
	return &core_map[pte];
}

/* Copy the contents of a page */
void
copy_page(struct page *src, struct page *dst)
{
	memcpy(dst,src,sizeof(struct page));
}

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

/* Calls function above. Might need to change later,
but works for KVAs at the time being 
//TODO use UIO? or word-aligned setting
for metter performance?? */
static
void
zero_page(size_t page_num)
{
	vaddr_t ptr = PADDR_TO_KVADDR(core_map[page_num].pa);

	memset((void*) ptr,'\0',PAGE_SIZE);
}

/* Called by kpage_alloc and page_nalloc for KERNEL pages*/
static
void 
allocate_fixed_page(size_t page_num)
{
	// KASSERT(lock_do_i_hold(core_map_lock));
	core_map[page_num].state = FIXED;
	paddr_t pa = page_num * PAGE_SIZE;
	core_map[page_num].pa = pa;
	core_map[page_num].va = 0x0;
	core_map[page_num].as = NULL;
	zero_page(page_num);
}

static
void
allocate_nonfixed_page(size_t page_num, struct addrspace *as, vaddr_t va)
{
	//Allocate a page
	core_map[page_num].state = DIRTY;
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
	pt->table[pt_index] = PAGEVA_TO_PTE(page_location	);

	zero_page(page_num);
}

/* Called by free_kpages */
static
void 
free_fixed_page(size_t page_num)
{
	// KASSERT(lock_do_i_hold(core_map_lock));
	core_map[page_num].state = FREE;
	core_map[page_num].va = 0x0;
	core_map[page_num].as =  NULL;
	core_map[page_num].npages = 0;
	// if(core_map[page_num].as != NULL)
	// {
	// 	panic("I don't know how to free page with an address space");
	// }
}

struct page *
page_alloc(struct addrspace* as, vaddr_t va)
{
	bool holdlock = spinlock_do_i_hold(&stealmem_lock);
	if(!holdlock)
	{
		spinlock_acquire(&stealmem_lock);
	}
	for(size_t i = 0;i<page_count;i++)
	{
		if(core_map[i].state == FREE)
		{
			if(as == NULL)
			{
				KASSERT(va == 0x0);
				allocate_fixed_page(i);				
			}
			else
			{
				KASSERT(va != 0x0);
				allocate_nonfixed_page(i,as,va);
			}
			core_map[i].npages = 1;
			if(!holdlock)
			{
				spinlock_release(&stealmem_lock);
			}
			return &core_map[i];
		}
	}
	spinlock_release(&stealmem_lock);
	panic("No available pages for single page alloc!");
	return 0x0;
}

static
vaddr_t
page_nalloc(int npages)
{
	spinlock_acquire(&stealmem_lock);
	bool blockStarted = false;
	int pagesFound = 0;
	int startingPage = 0;
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
			//Allocate the block of pages, now.
			for(int j = startingPage; j<startingPage + npages; j++)
			{
				allocate_fixed_page(j);
			}
			core_map[startingPage].npages = npages;
			spinlock_release(&stealmem_lock);
			return PADDR_TO_KVADDR(core_map[startingPage].pa);
		}
	}
	spinlock_release(&stealmem_lock);
	//TODO swap
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
		struct page *kern_page = page_alloc(0x0,0x0);
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

/* Free kernel heap pages (called by kfree) */
/* Only works for pages that are in a contiguous block */
void free_kpages(vaddr_t addr)
{
	if(addr < 0x80000000)
	{
		panic("I only support KVAs right now");	
	}
	spinlock_acquire(&stealmem_lock);
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
				free_fixed_page(j);
			}
			// kprintf("memory freed\n");
			spinlock_release(&stealmem_lock);
			return;
		}
	}
	panic("VA Doesn't exist!");
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void)
{
	return;
}
void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
}

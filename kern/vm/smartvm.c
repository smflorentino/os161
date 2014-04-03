/*

	A Smart Virtual Memory System
*/

#include <types.h>
#include <lib.h>
#include <vm.h>
#include <machine/vm.h>
#include <mainbus.h>
#include <spinlock.h>
/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static bool vm_initialized = false;

static struct page **core_map;
/* Initialization function */
void vm_bootstrap() 
{
	paddr_t firstaddr, lastaddr;
	ram_getsize(&firstaddr,&lastaddr);
	int page_count = lastaddr / PAGE_SIZE;
	core_map = (struct page**)PADDR_TO_KVADDR(firstaddr);
	paddr_t freeaddr = firstaddr + page_count * sizeof(struct page);
	vm_initialized = true;
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) 
{
	(void)faulttype;
	(void)faultaddress;
	return 0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages)
{
	if(!vm_initialized) {
		/* This code is from dumbvm */
		paddr_t addr;
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		if (addr==0) {
			return 0;
		}
		return PADDR_TO_KVADDR(addr);
	}
	(void)npages;
	vaddr_t t;
	return t;
}

void free_kpages(vaddr_t addr)
{
	(void)addr;
	return;
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

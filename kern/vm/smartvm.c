/*

	A Smart Virtual Memory System
*/

#include <types.h>
#include <lib.h>
#include <vm.h>
#include <machine/vm.h>
#include <mainbus.h>
#include <spinlock.h>
#include <uio.h>
#include <kern/iovec.h>
/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static bool vm_initialized = false;

static struct page *core_map;
static size_t page_count;
// static struct lock *core_map_lock = NULL;

/* Initialization function */
void vm_bootstrap() 
{
	paddr_t firstaddr, lastaddr;
	ram_getsize(&firstaddr,&lastaddr);
	page_count = lastaddr / PAGE_SIZE;
	core_map = (struct page*)PADDR_TO_KVADDR(firstaddr);
	paddr_t freeaddr = firstaddr + page_count * sizeof(struct page);
	kprintf("FirstAddr: %p\n", (void*) firstaddr);
	kprintf("Freeaddr: %p\n", (void*) freeaddr);
	kprintf("Size of Core Map: %d\n", page_count * sizeof(struct page));
	kprintf("Base Addr of core_map: %p\n", &core_map);
	kprintf("Base Addr of core_map[0]: %p\n", &core_map[0]);
	kprintf("Base Addr of core_map[127]: %p\n", &core_map[127]);
	//Calculate the number of fixed pages:
	size_t num_of_fixed_pages = (freeaddr / PAGE_SIZE);
	if(freeaddr % PAGE_SIZE != 0)
	{
		num_of_fixed_pages++;
	}
	//Mark everything from 0 to freeaddr as FIXED...
	for(size_t i = 0; i<num_of_fixed_pages; i++)
	{
		kprintf("Address of Core Map %d: %p ",i,&core_map[i]);
		kprintf("PA of Core Map %d:%p\n", i, (void*) (PAGE_SIZE * i));
		core_map[i].state = FIXED;
	}
	//Mark everything from freeaddr to lastaddr as FREE...
	for(size_t i = num_of_fixed_pages; i<page_count; i++)
	{
		kprintf("Address of Core Map %d: %p\n",i,&core_map[i]);
		kprintf("PA of Core Map %d:%p\n", i, (void*) (PAGE_SIZE * i));
		kprintf("KVA of Core Map %d:%p\n",i, (void*) PADDR_TO_KVADDR(PAGE_SIZE*i));
		core_map[i].state = FREE;
		core_map[i].as = 0x0;
		core_map[i].va = 0x0;
	}
	vm_initialized = true;
	(void)freeaddr;
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress) 
{
	(void)faulttype;
	(void)faultaddress;
	return 0;
}

static
void
memset(void *ptr, int ch, size_t len)
{
	char *p = ptr;
	int *x = ptr;
	(void)x;
	size_t i;
	
	for (i=0; i<len; i++) {
		p[i] = ch;
	}
	*x = 1;
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

static
void
zero_page(size_t page_num)
{
	vaddr_t ptr = core_map[page_num].va;

	memset((void*) ptr,'\0',PAGE_SIZE);
}

static
vaddr_t
kpage_alloc()
{
	spinlock_acquire(&stealmem_lock);
	for(size_t i = 0;i<page_count;i++)
	{
		//TODO use a lock
		if(core_map[i].state == FREE)
		{
			core_map[i].state = DIRTY;
			paddr_t pa = i * PAGE_SIZE;
			core_map[i].va = PADDR_TO_KVADDR(pa);
			core_map[i].as = NULL;
			zero_page(i);
			KASSERT(core_map[i].va != 0);
			spinlock_release(&stealmem_lock);
			return core_map[i].va;
		}
	}
	spinlock_release(&stealmem_lock);
	panic("No available pages for single page alloc!");
	return 0x0;
}

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
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
		return kpage_alloc();
	}
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

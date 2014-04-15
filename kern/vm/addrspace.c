/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <mips/tlb.h>
#include <spl.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	//Go through entries in page directory.
	for(size_t i = 0;i<PAGE_DIR_ENTRIES;i++)
	{
		//Get current entry in page directory.
		struct page_table *oldpt =  old->page_dir[i];
		//Page directory at index i points to a page table.
		if(oldpt != NULL)
		{
			//Create a new page table, and assign it.
			struct page_table *newpt = kmalloc(sizeof (struct page_table));
			newas->page_dir[i] = newpt;
			//Now iterate through each entry in the page table.
			//If a page exists, copy it; and update hte new table.
			for(size_t pte = 0; pte< PAGE_TABLE_ENTRIES;pte++)
			{
				int pt_entry = oldpt->table[pte];
				//Page Table Entry exists
				if(pt_entry != 0x0)
				{
					//Locate the old page
					struct page *oldpage = get_page(pt_entry);
					//Allocate a new page
					vaddr_t new_page_va = page_alloc(newas);
					//Copy the data:
					memcpy((void*) new_page_va, (void*) oldpage->va,PAGE_SIZE);

					//Update the new page table accordingly:
					int new_pt_entry = PAGEVA_TO_PTE(new_page_va);
					newpt->table[pte] = new_pt_entry;
					//TODO permissions
				}
				else //No page at Page Table entry 'pte'
				{
					newpt->table[pte] = 0x0;
				}
			}
		}
		else //Page Table does not exist at this index.
		{
				newas->page_dir[i] = NULL;
		}
	}
	
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	//Go through each entry in the page directory.
	for(size_t i = 0;i<PAGE_DIR_ENTRIES;i++)
	{
		//Get current entry in page directory.
		struct page_table *pt = as->page_dir[i];
		//If a page table exists in this entry, go through all page in that table.
		if(pt != NULL)
		{
			for(size_t j=0;j<PAGE_TABLE_ENTRIES;j++)
			{
				int pt_entry = pt->table[j];
				//If a page exists at this entry in the table, free it.
				if(pt_entry != 0x0)
				{
					struct page *page = get_page(pt_entry);
					free_kpages(page->va);
				}
			}
			//Now, delete the page table. //TODO create a destory method??
			kfree(pt);
		}
	}
	//Now, delete the page directory.
	// kfree(as->page_dir);
	//Now, delete the address space.
	kfree(as);
}

/* Shamelessly stolen from dumbvm.
 * It *should* simply invalidate all TLB entries.
 * We'll find out.
 */
void
as_activate(struct addrspace *as)
{
	int i, spl;

	(void)as;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t pages_required = sz / PAGE_SIZE;
	if(sz < PAGE_SIZE)
	{
		pages_required = 1;
	}
	else if(sz % PAGE_SIZE != 0)
	{
		pages_required++;
	}
	//Adjust the start of the heap.
	vaddr_t seg_end = vaddr + pages_required * PAGE_SIZE;
	if(seg_end > as->heap_start)
	{
		as->heap_start = seg_end;
		as->heap_end = as->heap_start;
	}

	vaddr_t cur_vaddr = vaddr;
	for(size_t i = 0; i < pages_required; i++)
	{
		//Allocate a page. //TODO implement on-demand paging.
		vaddr_t page_va = page_alloc(as);
		
		//Get the page table for the virtual address.
		struct page_table *pt = pgdir_walk(as,cur_vaddr,true);
		KASSERT(pt != NULL);
		KASSERT(pt != 0x0);

		//Update the page table entry to point to the page we made.
		size_t pt_index = VA_TO_PT_INDEX(cur_vaddr);
		pt->table[pt_index] = PAGEVA_TO_PTE(page_va);
		DEBUG(DB_VM,"Page VA:%p\n", (void*) cur_vaddr);
		DEBUG(DB_VM,"Page PA:%p\n", (void*) KVADDR_TO_PADDR(page_va));
		// kprintf("Page VA: %p\n",(void*) page_va);
		// kprintf("Page PA: %p\n",(void*) KVADDR_TO_PADDR(page_va));
		// kprintf("PTE: %d\n", PAGEVA_TO_PTE(page_va));
		//TODO permissions too

		cur_vaddr += PAGE_SIZE;
	}

	//Heap Moves up as we define each region
	as->heap_start += sz;
	//Also define the end of heap
	as->heap_end = as->heap_start;

	DEBUG(DB_VM,"Region VA: %p\n",(void*) vaddr);
	DEBUG(DB_VM,"Region SZ: %d\n", sz);
	DEBUG(DB_VM,"Page Count: %d\n",pages_required);
	DEBUG(DB_VM,"RWX: %d%d%d\n", readable,writeable,executable);
	// kprintf("Region VA:%p\n", (void*) vaddr);
	// kprintf("Region SZ:%d\n",sz);
	// kprintf("RWX:%d%d%d\n",readable,writeable,executable);

	//TODO
	(void)readable;
	(void)writeable;
	(void)executable;
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;
	as->stack = (vaddr_t) USERSTACK - PAGE_SIZE;
	//Allocate a page for the stack.
	vaddr_t stack_page_va = page_alloc(as);
	// kprintf("Allocating page for stack...\n");
	//Get the page table for the virtual address.
	struct page_table *pt = pgdir_walk(as,as->stack,true);
	KASSERT(pt != NULL);
	KASSERT(pt != 0x0);

	//Update the page table entry to point to the page we made.
	size_t pt_index = VA_TO_PT_INDEX(as->stack);
	pt->table[pt_index] = PAGEVA_TO_PTE(stack_page_va);
	DEBUG(DB_VM, "Heap End: %p\n", (void*) as->heap_end);
	DEBUG(DB_VM,"Stack:%p\n",(void*) as->stack);
	// kprintf("VA: %p\n",(void*) stackptr);
	// kprintf("Page PA: %p\n",(void*) KVADDR_TO_PADDR(stack_page_va));
	// kprintf("PTE: %d\n", PAGEVA_TO_PTE(stack_page_va));
	//TODO permissions too

	//Align the heap on a page boundary:
	int offset = as->heap_start % PAGE_SIZE;
	if(offset != 0)
	{
		as->heap_start += (PAGE_SIZE - offset);
		as->heap_end += (PAGE_SIZE -offset);
	}

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	
	return 0;
}


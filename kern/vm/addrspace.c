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
#include <elf.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/* Create an address space. We allocate space for a page directory and 
 * some other stuff in the address space struct. We ignore permissions,
 * and also allow load_elf to access any part of the address space as it loads
 * segments. It would probably be better to put some of this in as_prepare_load,
 * for better readability/style, but it works here.
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	//Use permissions (for now...)
	as->use_permissions = true;
	//load_elf will just be starting when we call as_create...
	as->loadelf_done = false;

	return as;
}

/* Copy a process's address space. This involves walking the 
 * page directory of the old address space, creating page tables
 * as needed, and copying any pages that are allocated. We also need
 * to copy permissions as well.
 */
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
			//If a page exists, copy it; and update the new table.
			for(size_t pte = 0; pte< PAGE_TABLE_ENTRIES;pte++)
			{
				int pt_entry = oldpt->table[pte];
				//Page Table Entry exists
				if(pt_entry != 0x0)
				{
					//Get page permissions
					int permissions = PTE_TO_PERMISSIONS(pt_entry);
					//Locate the old page (swap if in, if needed)
					struct page *oldpage = get_page(i,pte,pt_entry);
					//Allocate a new page
					struct page *newpage = page_alloc(newas,oldpage->va,permissions);
					vaddr_t new_page_va = PADDR_TO_KVADDR(newpage->pa);
					vaddr_t old_page_va = PADDR_TO_KVADDR(oldpage->pa);
					//Copy the data:
					memcpy((void*) new_page_va, (void*) old_page_va,PAGE_SIZE);
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

/* Destroy a process's address space. We need to walk the page
 * table, free any pages, and then free the appropriate data structures 
 * used by the VM system (the page directory, page tables, addrsapce struct).
 */
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
					int swapped = PTE_TO_LOCATION(pt_entry);
					//If swapped, we don't need to load the page.
					//But we do need to delete it from the swap file
					if(swapped)
					{
						//TODO remove page from swap file
						continue;
					}
					struct page *page = get_page(i,j,pt_entry);
					vaddr_t page_location = PADDR_TO_KVADDR(page->pa);
					free_kpages(page_location);
				}
			}
			//Now, delete the page table. //TODO create a destory method??
			kfree(pt);
		}
	}
	//Now, delete the address space.
	kfree(as);
}

/* Shamelessly stolen from dumbvm.
 * It simply invalidate all TLB entries.
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
	DEBUG(DB_VM, "Seg Start: %p\n", (void*) vaddr);
	DEBUG(DB_VM, "Size: %p\n", (void*) sz);
	//Calculate the number of pages that we need
	size_t pages_required = sz / PAGE_SIZE;
	if(sz < PAGE_SIZE)
	{
		pages_required = 1;
	}
	else if(sz % PAGE_SIZE != 0)
	{
		pages_required++;
	}
	/*If we split between two pages, add another page:
	 * For example, if vaddr = 0x444970, and size = 192,
	 * we need from 0x444970 to 0x445162. So we need
	 * 0x44000 AND 0x445000 -- TWO PAGES. Simply aligning 
	 * to a page boundary will get is 0x44000, but not the 
	 * 162 bytes that spill into the next page. So we need to
	 * account for that now:
	 */
	// if(((vaddr & SUB_FRAME) + sz > PAGE_SIZE))
	// {
	// 	// DEBUG(DB_VM, "Addr: %p Size: %d\n", (void*) vaddr,sz);
	// 	// DEBUG(DB_VM, "Offest: %p\n", (void*) (vaddr & SUB_FRAME));
	// 	pages_required++;
	// }
	DEBUG(DB_VM,"Pages Required:%d\n", pages_required);
	DEBUG(DB_VM,"Seg End:%p\n", (void*) ( vaddr + (pages_required * PAGE_SIZE)));
	DEBUG(DB_VM,"Seg End:%p\n", (void*) (vaddr + sz));

	//Adjust the start of the heap to be *PAST* the end of this segment.
	vaddr_t seg_end = vaddr + pages_required * PAGE_SIZE;
	if(seg_end > as->heap_start)
	{
		as->heap_start = seg_end + PAGE_SIZE;
		as->heap_end = as->heap_start;
	}

	//Align the vaddr on a page boundary.
	DEBUG(DB_VM, "addr: %p size: %d\n", (void*) vaddr,sz);
	vaddr &= PAGE_FRAME;
	DEBUG(DB_VM, "addr: %p size: %d\n",(void*) vaddr,sz);
	//Allocate the segment, one page at a time. Pages need not be contiguous.
	vaddr_t cur_vaddr = vaddr;
	for(size_t i = 0; i < pages_required; i++)
	{
		DEBUG(DB_VM, "CURVA:%p\n", (void*) cur_vaddr);
		int permissions = readable | writeable | executable;
		struct page *page = page_alloc(as,cur_vaddr,permissions);
		(void) page;

		cur_vaddr += PAGE_SIZE;
	}

	return 0;
}

/* Called from load_elf before segments are loaded. We need to tell
 * the system to ignore permissions so we can actually load stuff into
 * read-only segments
 */
int
as_prepare_load(struct addrspace *as)
{
	as->use_permissions = false;
	return 0;
}

/* Called from load_elf after the segments are loaded. We need to tell
 * the system to use permissions as well as invalidate any TLB entries
 * that were created when we were ignoring permissions. That way,
 * the next time we access memory, the TLB will be populated with the 
 * proper permissions (when use_permissions is disabled, every entry in the TLB is
 * enabled for writing)
 */
int
as_complete_load(struct addrspace *as)
{
	//Enable permissions.
	as->use_permissions = true;
	DEBUG(DB_VM, "Load complete.\n");
	//Clear the TLB
	as_activate(as);
	return 0;
}

/* Called from runprogram.c to create a user stack. The stack grows down,
 * but obvously we allocate memory going up. So our as->stack points to the 
 * "bottom" of the stack, farthest down the stack can grow. We allocate a single page
 * for the stack now; if we need more, we'll allocate it later in vm_fault.
 *
 * We also align the heap to a page boundary now, so malloc doesn't have to do it 
 * later. We can do this at this point because by the time this is called, all the 
 * static regions have been defined. I suppose it *could* be done in as_complete_load
 * instead, but if it ain't broke, don't fix it ;)
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	//Get the lowest address of the stack
	as->stack = (vaddr_t) USERSTACK - PAGE_SIZE;
	//Set permissions
	int permissions = PF_RWX; //change?
	//Allocate the page
	struct page *page = page_alloc(as,as->stack,permissions);
	(void) page;
	DEBUG(DB_VM,"Heap Start before aligning: %p\n", (void*) as->heap_end);
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


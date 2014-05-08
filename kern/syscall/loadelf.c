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


/*
 * Code to load an ELF-format executable into the current address space.
 *
 * It makes the following address space calls:
 *    - first, as_define_region once for each segment of the program;
 *    - then, as_prepare_load;
 *    - then it loads each chunk of the program;
 *    - finally, as_complete_load.
 *
 * This gives the VM code enough flexibility to deal with even grossly
 * mis-linked executables if that proves desirable. Under normal
 * circumstances, as_prepare_load and as_complete_load probably don't
 * need to do anything.
 *
 * If you wanted to support memory-mapped executables you would need
 * to rearrange this to map each segment.
 *
 * To support dynamically linked executables with shared libraries
 * you'd need to change this to load the "ELF interpreter" (dynamic
 * linker). And you'd have to write a dynamic linker...
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>

/*
 * Load a segment at virtual address VADDR. The segment in memory
 * extends from VADDR up to (but not including) VADDR+MEMSIZE. The
 * segment on disk is located at file offset OFFSET and has length
 * FILESIZE.
 *
 * FILESIZE may be less than MEMSIZE; if so the remaining portion of
 * the in-memory segment should be zero-filled.
 *
 * Note that uiomove will catch it if someone tries to load an
 * executable whose load address is in kernel space. If you should
 * change this code to not use uiomove, be sure to check for this case
 * explicitly
 */
static
int
load_segment(struct vnode *v, off_t offset, vaddr_t vaddr, 
	     size_t memsize, size_t filesize,
	     int is_executable)
{
	struct iovec iov;
	struct uio u;
	int result;

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	DEBUG(DB_DEMAND, "ELF: Loading %lu bytes to 0x%lx\n", 
	      (unsigned long) filesize, (unsigned long) vaddr);

	iov.iov_ubase = (userptr_t)vaddr;
	iov.iov_len = memsize;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize;          // amount to read from the file
	u.uio_offset = offset;
	u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curthread->t_addrspace;

	result = VOP_READ(v, &u);
	if (result) {
		return result;
	}

	if (u.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * If memsize > filesize, the remaining space should be
	 * zero-filled. There is no need to do this explicitly,
	 * because the VM system should provide pages that do not
	 * contain other processes' data, i.e., are already zeroed.
	 *
	 * During development of your VM system, it may have bugs that
	 * cause it to (maybe only sometimes) not provide zero-filled
	 * pages, which can cause user programs to fail in strange
	 * ways. Explicitly zeroing program BSS may help identify such
	 * bugs, so the following disabled code is provided as a
	 * diagnostic tool. Note that it must be disabled again before
	 * you submit your code for grading.
	 */
#if 0
	{
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0) {
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n", 
			      (unsigned long) fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif
	
	return result;
}

/* Load a Segment Dynamically */
static
int
dynamic_load_segment(struct vnode *v, off_t offset, vaddr_t vaddr, 
	     size_t memsize, size_t filesize,
	     int is_executable, int permissions)
{
	/*If we split between two pages, add another page:
	 * For example, if vaddr = 0x444970, and size = 192,
	 * we need from 0x444970 to 0x445162. So we need
	 * 0x44000 AND 0x445000 -- TWO PAGES. Simply aligning 
	 * to a page boundary will get is 0x44000, but not the 
	 * 162 bytes that spill into the next page. So we need to
	 * account for that now:
	 */
	int result;
	if(((vaddr & SUB_FRAME) + filesize > PAGE_SIZE))
	{
		DEBUG(DB_DEMAND, "Spillover Detected\n");
		/*Copy the first part of the page*/
		struct page *page = page_alloc(curthread->t_addrspace, vaddr & PAGE_FRAME,permissions);
		//Get amount to copy
		size_t amt_to_copy = PAGE_SIZE - (vaddr & SUB_FRAME);
		//Copy the rest	
		result = load_segment(v,offset,vaddr,amt_to_copy,amt_to_copy,is_executable);
		page->state = DIRTY;
		if(result) { return result; }
		/* Copy the second part of the page */
		vaddr_t spillover_va = ((vaddr & PAGE_FRAME) + PAGE_SIZE);
		bool lock = get_coremap_lock();
		page = page_alloc(curthread->t_addrspace, spillover_va,permissions);
		release_coremap_lock(lock);
		result = load_segment(v,offset + amt_to_copy, vaddr, memsize - amt_to_copy, filesize - amt_to_copy,is_executable);
		if(result) { return result;}
		KASSERT(page->state == LOCKED);
		page->state = DIRTY;
	}
	else
	{
		struct page *page = page_alloc(curthread->t_addrspace,vaddr & PAGE_FRAME,permissions);
		result = load_segment(v,offset,vaddr,memsize,filesize,is_executable);
		KASSERT(page->state == LOCKED);
		page->state = DIRTY;
		return result;
	}
	return 0;
}

/*
 * Load an ELF executable user program into the current address space.
 *
 * Returns the entry point (initial PC) for the program in ENTRYPOINT.
 */
int
load_elf(struct vnode *v, vaddr_t *entrypoint)
{
	Elf_Ehdr eh;   /* Executable header */
	Elf_Phdr ph;   /* "Program header" = segment header */
	int result, i;
	struct iovec iov;
	struct uio ku;
	struct addrspace *as = curthread->t_addrspace;

	/*
	 * Read the executable header from offset 0 in the file.
	 */

	uio_kinit(&iov, &ku, &eh, sizeof(eh), 0, UIO_READ);
	result = VOP_READ(v, &ku);
	if (result) {
		return result;
	}

	if (ku.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on header - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * Check to make sure it's a 32-bit ELF-version-1 executable
	 * for our processor type. If it's not, we can't run it.
	 *
	 * Ignore EI_OSABI and EI_ABIVERSION - properly, we should
	 * define our own, but that would require tinkering with the
	 * linker to have it emit our magic numbers instead of the
	 * default ones. (If the linker even supports these fields,
	 * which were not in the original elf spec.)
	 */

	if (eh.e_ident[EI_MAG0] != ELFMAG0 ||
	    eh.e_ident[EI_MAG1] != ELFMAG1 ||
	    eh.e_ident[EI_MAG2] != ELFMAG2 ||
	    eh.e_ident[EI_MAG3] != ELFMAG3 ||
	    eh.e_ident[EI_CLASS] != ELFCLASS32 ||
	    eh.e_ident[EI_DATA] != ELFDATA2MSB ||
	    eh.e_ident[EI_VERSION] != EV_CURRENT ||
	    eh.e_version != EV_CURRENT ||
	    eh.e_type!=ET_EXEC ||
	    eh.e_machine!=EM_MACHINE) {
		return ENOEXEC;
	}

	/*
	 * Go through the list of segments and set up the address space.
	 *
	 * Ordinarily there will be one code segment, one read-only
	 * data segment, and one data/bss segment, but there might
	 * conceivably be more. You don't need to support such files
	 * if it's unduly awkward to do so.
	 *
	 * Note that the expression eh.e_phoff + i*eh.e_phentsize is 
	 * mandated by the ELF standard - we use sizeof(ph) to load,
	 * because that's the structure we know, but the file on disk
	 * might have a larger structure, so we must use e_phentsize
	 * to find where the phdr starts.
	 */
	DEBUG(DB_DEMAND,"Num Of Segements:%d\n",eh.e_phnum);
	for (i=0; i<eh.e_phnum; i++) {
		off_t offset = eh.e_phoff + i*eh.e_phentsize;
		uio_kinit(&iov, &ku, &ph, sizeof(ph), offset, UIO_READ);

		result = VOP_READ(v, &ku);
		if (result) {
			return result;
		}

		if (ku.uio_resid != 0) {
			/* short read; problem with executable? */
			kprintf("ELF: short read on phdr - file truncated?\n");
			return ENOEXEC;
		}

		switch (ph.p_type) {
		    case PT_NULL: /* skip */ continue;
		    case PT_PHDR: /* skip */ continue;
		    case PT_MIPS_REGINFO: /* skip */ continue;
		    case PT_LOAD: break;
		    default:
			kprintf("loadelf: unknown segment type %d\n", 
				ph.p_type);
			return ENOEXEC;
		}
		DEBUG(DB_DEMAND, "MEM SZ:%d\n",ph.p_memsz);
		DEBUG(DB_DEMAND, "FILE SZ:%d\n", ph.p_filesz);
		DEBUG(DB_DEMAND, "VADDR:%p\n",(void*)ph.p_vaddr);
		result = as_define_region(curthread->t_addrspace,
					  ph.p_vaddr, ph.p_memsz,
					  ph.p_flags & PF_R,
					  ph.p_flags & PF_W,
					  ph.p_flags & PF_X);
		if (result) {
			return result;
		}
	}

	result = as_prepare_load(curthread->t_addrspace);
	if (result) {
		return result;
	}

	/*
	 * Now actually load each segment.
	 */
	for (i=0; i<eh.e_phnum; i++) {
		off_t offset = eh.e_phoff + i*eh.e_phentsize;
		uio_kinit(&iov, &ku, &ph, sizeof(ph), offset, UIO_READ);
		DEBUG(DB_DEMAND, "SEGMENT: %d\n",1);
		result = VOP_READ(v, &ku);
		if (result) {
			return result;
		}

		if (ku.uio_resid != 0) {
			/* short read; problem with executable? */
			kprintf("ELF: short read on phdr - file truncated?\n");
			return ENOEXEC;
		}

		switch (ph.p_type) {
		    case PT_NULL: /* skip */ continue;
		    case PT_PHDR: /* skip */ continue;
		    case PT_MIPS_REGINFO: /* skip */ continue;
		    case PT_LOAD: break;
		    default:
			kprintf("loadelf: unknown segment type %d\n", 
				ph.p_type);
			return ENOEXEC;
		}
		struct addrspace *as = curthread->t_addrspace;
		vaddr_t va = ph.p_vaddr;
		DEBUG(DB_DEMAND, "Original VA: %p\n", (void*) va);
		DEBUG(DB_DEMAND, "Max VA:%p\n", (void*) (va + ph.p_memsz));
		// va &= PAGE_FRAME;
		struct page_table *pt = pgdir_walk(as,va,false);
		int pt_index = VA_TO_PT_INDEX(va);
		int permissions = PTE_TO_PERMISSIONS(pt->table[pt_index]);
		int small_pages = 0;
		if(ph.p_memsz <= PAGE_SIZE)
		{
			DEBUG(DB_DEMAND, "Segement Total Size is < 4k\n");
			// pt = pgdir_walk(as,va,false);
			// pt_index = VA_TO_PT_INDEX(va);
			// permissions = PTE_TO_PERMISSIONS(pt->table[pt_index]);


			result = dynamic_load_segment(v, ph.p_offset, va, 
				ph.p_memsz, ph.p_filesz,
				ph.p_flags & PF_X,permissions);
			if (result) {
					return result;
			}
			// page->state = DIRTY;	
			small_pages = 1;
		}
		else
		{
			int filesize = (int) ph.p_filesz;
			int memsize = (int) ph.p_memsz;
			off_t cur_offset = ph.p_offset;
			DEBUG(DB_DEMAND, "Loading >1 Segment\n");
			DEBUG(DB_DEMAND, "Original Parameters:\n");
			DEBUG(DB_DEMAND, "File Size: %d\n",filesize);
			DEBUG(DB_DEMAND, "Mem Size: %d\n", memsize);
			DEBUG(DB_DEMAND, "Offset:%d\n", (int) cur_offset);
			DEBUG(DB_DEMAND, "VA:%p\n", (void*) va);
			size_t amt_to_read;
			int pages = 0;
			int blank_pages = 0;
			while(filesize > 0)
			{
				// struct page *page = page_alloc(as,va & PAGE_FRAME,permissions);
				DEBUG(DB_DEMAND, "loading...\n");
				DEBUG(DB_DEMAND, "File Size: %d\n",filesize);
				DEBUG(DB_DEMAND, "Mem Size: %d\n", memsize);
				DEBUG(DB_DEMAND, "Offset:%d\n", (int) cur_offset);
				DEBUG(DB_DEMAND, "VA:%p\n", (void*) va);
				amt_to_read = (filesize - PAGE_SIZE > 0 ? PAGE_SIZE : filesize);
				result = dynamic_load_segment(v, cur_offset, va, 
		      		PAGE_SIZE, amt_to_read, ph.p_flags & PF_X,permissions);
				if (result) {
						return result;
				}
				filesize -= PAGE_SIZE;
				memsize -= PAGE_SIZE;
				va += PAGE_SIZE;
				cur_offset += PAGE_SIZE;
				// page->state = DIRTY;
				pages++;
			}
			(void)blank_pages;
			DEBUG(DB_DEMAND,"Empty Pages...\n");
			while(memsize > 0)
			{
				DEBUG(DB_DEMAND,"Mem Size:%d\n",memsize);
				DEBUG(DB_DEMAND,"VA: %p\n",(void*) va);
				bool lock = get_coremap_lock();
				struct page *page = page_alloc(as,va & PAGE_FRAME,permissions);
				release_coremap_lock(lock);
				va += PAGE_SIZE;
				memsize -= PAGE_SIZE;
				KASSERT(page->state == LOCKED);
				page->state = DIRTY;
				blank_pages++;
			}
			DEBUG(DB_DEMAND, "Small Pages: %d\n", small_pages);
			DEBUG(DB_DEMAND, "Pages: %d\n", pages);
			DEBUG(DB_DEMAND, "Blank Pages: %d\n", blank_pages);

		}
		// result = load_segment(v, ph.p_offset, ph.p_vaddr, 
		//       ph.p_memsz, ph.p_filesz,
		//       ph.p_flags & PF_X);
		if (result) {
			return result;
		}
	}

	result = as_complete_load(curthread->t_addrspace);
	if (result) {
		return result;
	}

	*entrypoint = eh.e_entry;

	/* Register load complete in addrspace */
	as->loadelf_done = true;
	DEBUG(DB_VM,"LoadELFDone\n");
	// kprintf("LoadELF done\n");
	return 0;
}

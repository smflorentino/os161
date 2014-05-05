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

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */

#include <machine/vm.h>
#include <addrspace.h>

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

 typedef enum {
 	FREE,/*0*/
 	CLEAN,/*1*/
 	DIRTY,/*2*/
 	FIXED,/*3*/
 	SWAPPINGIN,/*4*/
 	SWAPPINGOUT,/*5*/
 	LOCKED,/*6*/
 	LOADING /*7*/
 } page_state_t;

 struct page {
 	/* Where the page is mapped to */		
 	struct addrspace* as;
 	vaddr_t va;

 	/* Location in physical memory*/
 	paddr_t pa;

 	/* Number of pages allocated (currently used by  kpage_nalloc) */
 	size_t npages;
 	/* Page state */
 	page_state_t state;
 };


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

/* Allocate a Page. Called by method above & by the address space*/
struct page * page_alloc(struct addrspace *as, vaddr_t va, int permissions);

/* Given an address space & and a virtual address, get a page table*/
struct page_table * pgdir_walk(struct addrspace *as, vaddr_t va, bool shouldcreate);

/* Given a page table entry, return a page */
struct page * get_page(int pdi,int pti,struct page_table *pt);

/* Copy a page */
void copy_page(struct page *src, struct page *dst);

/* Functions to get core map lock*/
bool get_coremap_spinlock(void);
void release_coremap_spinlock(bool);
bool coremap_spinlock_do_i_hold(void);

/* Functions to get coremap lock*/
bool get_coremap_lock(void);
void release_coremap_lock(bool);
bool coremap_lock_do_i_hold(void);

void unlock_loading_pages(struct addrspace *as);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void);
void vm_tlbshootdown(const struct tlbshootdown *);


#endif /* _VM_H_ */

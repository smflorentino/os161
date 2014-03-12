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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * Calculates the size of the kargs buffer. The kargs buffer must
 * be a contiguous block of memory that contains:
 * a pointers to the program arguments, followed by a NULL pointer;
 * the arguments themselves, each terminated by \0, with additional padding if
 * needed so they fit at 4 byte offsets.
 *
 * Note that this must be a contiguous block of memory, hence the padding.
 * Padding of the pointers is not needed, because in MIPS, pointers are 4
 * bytes long. We pad the arguments since we're storing everything in an 
 * array and we need to line everything up properly
 */
static
int
calculate_kargsd_size(char** args, unsigned long nargs)
{
	//Storage for each arg is strlen(arg) + 1 (for \0), padded accordingly to 4 byte intervals.
	//i.e.  ab becomes ab\0\0
	int size = 0;
	//Space for the pointers (plus the NULL one)
	size += (nargs+1);
	//Space for the padded arguments:
	for(size_t i=0;i<nargs;i++) 
	{
		int stringLength = strlen(args[i]) + 1;
		if( stringLength % 4 == 0) {
			size += (stringLength/4); //String fits nicely into 4 byte interval
		}
		else {
			size += (stringLength/4);
			size++; //We need to spill into next 4 byte block
		}
	}
	return size;
}

// static
// void
// test(void** a[])
// {
// 	(void)a;
// }

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}



/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname (args[0]) and thus may destroy it.
 * So, we copy it.
 */
int
runprogram1(const char* program, char** args, unsigned long nargs)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	char progname[128];
	strcpy(progname, program);
	(void)nargs;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {       
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	/* Copy program arguments to the kernel buffer*/

	//Calculate the size of the kernel buffer
	// (void)curarg_split1;
	size_t size = calculate_kargsd_size(args, nargs);
	//Create it
	void* kargv[size];
	int arg_ptrs[nargs];
	//Actually Copy the args into the kernel buffer
	//Add the NULL pointer (comes after the last arg)
	kargv[nargs] = NULL;
	//Now populate the data region
	int curblock = nargs+1; //right after NULL, this the position in kargv.
	int curblock_offset = 0; //how far we are in to each block of each position of kargv.
	int arg_start_pos = curblock;
	for(size_t i=0;i<nargs;i++)
	{
		// kprintf("Current arg:%s\n",args[i]);
		char* curarg = args[i]; //current arg from args
		char curarg_split[4]; //= {'a','a','a','a'}; //4 byte buffer
		char curarg_char = curarg[0];
		for(int j=0; true ;j++) //copy current arg one char at a time, until we hit '\0'
		{
			if(curblock_offset == 4)
			{
				/*we copied 4 chars. add the buffer to kargv, increment kargv position,
				, and reset the offset into the buffer */
				// kprintf("Copying \'%c%c%c%c\'to block%d\n", curarg_split[0],curarg_split[1],curarg_split[2],curarg_split[3],curblock);
				memcpy( &kargv[curblock], curarg_split, 4);
				// kargv[curblock] = (void*) curarg_split;
				curblock_offset = 0;
				curblock++;
			}

			// kprintf("Current char:%c\n", curarg[j]);
			curarg_char = curarg[j];
			if(curarg_char == '\0')
			{
				// kprintf("copying terminator..\n");
				curarg_split[curblock_offset] = '\0';
				curblock_offset++;
				break;
			}

			// kprintf("Current block:%d\n",curblock);	
			//copy current char into the 4-byte buffer, increment index.
			// curarg_char = curarg[j];
			curarg_split[curblock_offset] = curarg_char;
			curblock_offset++;
		}
		//Pad the remaining block, if it exists, and copy it into kargv.
		if(curblock_offset > 0)
		{
			for(int j=curblock_offset;j<4;j++)
			{
				// kprintf("padding...\n");
				curarg_split[curblock_offset] = '\0';
				curblock_offset++;
			}
			// kprintf("Copying \'%c%c%c%c\'to block %d\n", curarg_split[0],curarg_split[1],curarg_split[2],curarg_split[3],curblock);
			memcpy( &kargv[curblock], curarg_split, 4);
			// kargv[curblock] = (void*) curarg_split;
			curblock++;
			curblock_offset = 0;
		}
		//Now, set the pointer in kargv to point to the correct data region:
		//Note that i is the current arg, and kargv[i+1] should always be NULL. 
		kargv[i] = (void*) &kargv[arg_start_pos]; 
		arg_ptrs[i] = arg_start_pos;
		// kprintf("Contents of block %d: %s\n",i, x);
		// kprintf("assigning arg %d to point to block %d\n",i,arg_start_pos);
		//The next arg, if it exists, will begin at curblock now. 
		arg_start_pos = curblock; 
	}
	for(size_t i = 0;i<size;i++)
	{
		if(i <= nargs){
			// kprintf("Index: %d Value:%s Location:%p\n",i, (char*) kargv[i],&kargv[i]);

		}
		else{
			
			// kprintf("Index: %d VAlue:%p Location:%p\n",i, kargv[i],&kargv[i]);

		}
	}

	/*Copy the Kernel onto the Stack */
	int bufSize = size * sizeof(void*);

	// kprintf("stackptr:%p\n",(void*) stackptr);
	for(size_t i =0;i<nargs;i++)
	{
		// kprintf("Arg %d points to %d\n",i,arg_ptrs[i]);
		kargv[i] = (void*) (stackptr - bufSize) + sizeof(void*) * arg_ptrs[i];
	}
	result = copyout(kargv, (userptr_t) stackptr-bufSize,bufSize);
	if(result)
	{
		return result;
	}

	stackptr -= bufSize;
	// userptr_t argvptr =  (userptr_t) stackptr;

	// kprintf("Size%d\n",size);
	// kprintf("\nstackptr now: %p\n",(void*) stackptr);
	// kprintf("argv pointer now: %p\n",(void*) argvptr);
	// kprintf("address of argv[0] now: %p\n", &argv[0]);
	// kprintf("argv[0] now: %p\n", argv[0]);
	// kprintf("argv[0] now: %s\n", (char*) argv[0]);
	// kprintf("Value of argv:%p\n", argv);
	// kprintf("Address of argv[0]:%p\n",&argv[0]);
	/* Warp to user mode. */
		enter_new_process(nargs, (userptr_t) stackptr ,
				  stackptr, entrypoint);
	//enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	//		  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname (args[0]) and thus may destroy it.
 * So, we copy it.
 */
int
runprogram2(const char* program, char** args, unsigned long nargs)
{
	// kprintf("entering runprogram2\n");
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	char progname[128];
	strcpy(progname, program);
	(void)nargs;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}
	as_destroy(curthread->t_addrspace);
	curthread->t_addrspace = NULL;
	// /* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	// /* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {       
		vfs_close(v);
		return ENOMEM;
	}

	// /* Activate it. */
	// as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	/* Copy program arguments to the kernel buffer*/

	//Calculate the size of the kernel buffer
	// (void)curarg_split1;
	size_t size = calculate_kargsd_size(args, nargs);
	//Create it
	void* kargv[size];
	int arg_ptrs[nargs];
	//Actually Copy the args into the kernel buffer
	//Add the NULL pointer (comes after the last arg)
	kargv[nargs] = NULL;
	//Now populate the data region
	int curblock = nargs+1; //right after NULL, this the position in kargv.
	int curblock_offset = 0; //how far we are in to each block of each position of kargv.
	int arg_start_pos = curblock;
	for(size_t i=0;i<nargs;i++)
	{
		// kprintf("Current arg:%s\n",args[i]);
		char* curarg = args[i]; //current arg from args
		char curarg_split[4]; //= {'a','a','a','a'}; //4 byte buffer
		char curarg_char = curarg[0];
		for(int j=0; true ;j++) //copy current arg one char at a time, until we hit '\0'
		{
			if(curblock_offset == 4)
			{
				/*we copied 4 chars. add the buffer to kargv, increment kargv position,
				, and reset the offset into the buffer */
				// kprintf("Copying \'%c%c%c%c\'to block%d\n", curarg_split[0],curarg_split[1],curarg_split[2],curarg_split[3],curblock);
				memcpy( &kargv[curblock], curarg_split, 4);
				// kargv[curblock] = (void*) curarg_split;
				curblock_offset = 0;
				curblock++;
			}

			// kprintf("Current char:%c\n", curarg[j]);
			curarg_char = curarg[j];
			if(curarg_char == '\0')
			{
				// kprintf("copying terminator..\n");
				curarg_split[curblock_offset] = '\0';
				curblock_offset++;
				break;
			}

			// kprintf("Current block:%d\n",curblock);	
			//copy current char into the 4-byte buffer, increment index.
			// curarg_char = curarg[j];
			curarg_split[curblock_offset] = curarg_char;
			curblock_offset++;
		}
		//Pad the remaining block, if it exists, and copy it into kargv.
		if(curblock_offset > 0)
		{
			for(int j=curblock_offset;j<4;j++)
			{
				// kprintf("padding...\n");
				curarg_split[curblock_offset] = '\0';
				curblock_offset++;
			}
			// kprintf("Copying \'%c%c%c%c\'to block %d\n", curarg_split[0],curarg_split[1],curarg_split[2],curarg_split[3],curblock);
			memcpy( &kargv[curblock], curarg_split, 4);
			// kargv[curblock] = (void*) curarg_split;
			curblock++;
			curblock_offset = 0;
		}
		//Now, set the pointer in kargv to point to the correct data region:
		//Note that i is the current arg, and kargv[i+1] should always be NULL. 
		kargv[i] = (void*) &kargv[arg_start_pos]; 
		arg_ptrs[i] = arg_start_pos;
		// kprintf("Contents of block %d: %s\n",i, x);
		// kprintf("assigning arg %d to point to block %d\n",i,arg_start_pos);
		//The next arg, if it exists, will begin at curblock now. 
		arg_start_pos = curblock; 
	}
	for(size_t i = 0;i<size;i++)
	{
		if(i <= nargs){
			// kprintf("Index: %d Value:%s Location:%p\n",i, (char*) kargv[i],&kargv[i]);

		}
		else{
			
			// kprintf("Index: %d VAlue:%p Location:%p\n",i, kargv[i],&kargv[i]);

		}
	}

	/*Copy the Kernel onto the Stack */
	int bufSize = size * sizeof(void*);

	// kprintf("stackptr:%p\n",(void*) stackptr);
	for(size_t i =0;i<nargs;i++)
	{
		// kprintf("Arg %d points to %d\n",i,arg_ptrs[i]);
		kargv[i] = (void*) (stackptr - bufSize) + sizeof(void*) * arg_ptrs[i];
	}
	result = copyout(kargv, (userptr_t) stackptr-bufSize,bufSize);
	if(result)
	{
		return result;
	}

	stackptr -= bufSize;
	// userptr_t argvptr =  (userptr_t) stackptr;

	// kprintf("Size%d\n",size);
	// kprintf("\nstackptr now: %p\n",(void*) stackptr);
	// kprintf("argv pointer now: %p\n",(void*) argvptr);
	// kprintf("address of argv[0] now: %p\n", &argv[0]);
	// kprintf("argv[0] now: %p\n", argv[0]);
	// kprintf("argv[0] now: %s\n", (char*) argv[0]);
	// kprintf("Value of argv:%p\n", argv);
	// kprintf("Address of argv[0]:%p\n",&argv[0]);
	/* Warp to user mode. */
		enter_new_process(nargs, (userptr_t) stackptr ,
				  stackptr, entrypoint);
	//enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	//		  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname (args[0]) and thus may destroy it.
 * So, we copy it.
 */
int
runprogram3(const char* program, void* args, unsigned long nargs)
{
	// kprintf("entering runprogram2\n");
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	char progname[128];
	strcpy(progname, program);
	(void)nargs;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}
	as_destroy(curthread->t_addrspace);
	curthread->t_addrspace = NULL;
	// /* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	// /* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {       
		vfs_close(v);
		return ENOMEM;
	}

	// /* Activate it. */
	// as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	/* Copy program arguments to the kernel buffer*/
	(void) args;
	// //Calculate the size of the kernel buffer
	// // (void)curarg_split1;
	// size_t size = calculate_kargsd_size(args, nargs);
	// //Create it
	// void* kargv[size];
	// int arg_ptrs[nargs];
	// //Actually Copy the args into the kernel buffer
	// //Add the NULL pointer (comes after the last arg)
	// kargv[nargs] = NULL;
	// //Now populate the data region
	// int curblock = nargs+1; //right after NULL, this the position in kargv.
	// int curblock_offset = 0; //how far we are in to each block of each position of kargv.
	// int arg_start_pos = curblock;
	// for(size_t i=0;i<nargs;i++)
	// {
	// 	// kprintf("Current arg:%s\n",args[i]);
	// 	char* curarg = args[i]; //current arg from args
	// 	char curarg_split[4]; //= {'a','a','a','a'}; //4 byte buffer
	// 	char curarg_char = curarg[0];
	// 	for(int j=0; true ;j++) //copy current arg one char at a time, until we hit '\0'
	// 	{
	// 		if(curblock_offset == 4)
	// 		{
	// 			/*we copied 4 chars. add the buffer to kargv, increment kargv position,
	// 			, and reset the offset into the buffer */
	// 			// kprintf("Copying \'%c%c%c%c\'to block%d\n", curarg_split[0],curarg_split[1],curarg_split[2],curarg_split[3],curblock);
	// 			memcpy( &kargv[curblock], curarg_split, 4);
	// 			// kargv[curblock] = (void*) curarg_split;
	// 			curblock_offset = 0;
	// 			curblock++;
	// 		}

	// 		// kprintf("Current char:%c\n", curarg[j]);
	// 		curarg_char = curarg[j];
	// 		if(curarg_char == '\0')
	// 		{
	// 			// kprintf("copying terminator..\n");
	// 			curarg_split[curblock_offset] = '\0';
	// 			curblock_offset++;
	// 			break;
	// 		}

	// 		// kprintf("Current block:%d\n",curblock);	
	// 		//copy current char into the 4-byte buffer, increment index.
	// 		// curarg_char = curarg[j];
	// 		curarg_split[curblock_offset] = curarg_char;
	// 		curblock_offset++;
	// 	}
	// 	//Pad the remaining block, if it exists, and copy it into kargv.
	// 	if(curblock_offset > 0)
	// 	{
	// 		for(int j=curblock_offset;j<4;j++)
	// 		{
	// 			// kprintf("padding...\n");
	// 			curarg_split[curblock_offset] = '\0';
	// 			curblock_offset++;
	// 		}
	// 		// kprintf("Copying \'%c%c%c%c\'to block %d\n", curarg_split[0],curarg_split[1],curarg_split[2],curarg_split[3],curblock);
	// 		memcpy( &kargv[curblock], curarg_split, 4);
	// 		// kargv[curblock] = (void*) curarg_split;
	// 		curblock++;
	// 		curblock_offset = 0;
	// 	}
	// 	//Now, set the pointer in kargv to point to the correct data region:
	// 	//Note that i is the current arg, and kargv[i+1] should always be NULL. 
	// 	kargv[i] = (void*) &kargv[arg_start_pos]; 
	// 	arg_ptrs[i] = arg_start_pos;
	// 	// kprintf("Contents of block %d: %s\n",i, x);
	// 	// kprintf("assigning arg %d to point to block %d\n",i,arg_start_pos);
	// 	//The next arg, if it exists, will begin at curblock now. 
	// 	arg_start_pos = curblock; 
	// }
	// for(size_t i = 0;i<size;i++)
	// {
	// 	if(i <= nargs){
	// 		// kprintf("Index: %d Value:%s Location:%p\n",i, (char*) kargv[i],&kargv[i]);

	// 	}
	// 	else{
			
	// 		// kprintf("Index: %d VAlue:%p Location:%p\n",i, kargv[i],&kargv[i]);

	// 	}
	// }

	// /*Copy the Kernel onto the Stack */
	// int bufSize = size * sizeof(void*);

	// // kprintf("stackptr:%p\n",(void*) stackptr);
	// for(size_t i =0;i<nargs;i++)
	// {
	// 	// kprintf("Arg %d points to %d\n",i,arg_ptrs[i]);
	// 	kargv[i] = (void*) (stackptr - bufSize) + sizeof(void*) * arg_ptrs[i];
	// }
	// result = copyout(kargv, (userptr_t) stackptr-bufSize,bufSize);
	// if(result)
	// {
	// 	return result;
	// }

	// stackptr -= bufSize;
	// userptr_t argvptr =  (userptr_t) stackptr;

	// kprintf("Size%d\n",size);
	// kprintf("\nstackptr now: %p\n",(void*) stackptr);
	// kprintf("argv pointer now: %p\n",(void*) argvptr);
	// kprintf("address of argv[0] now: %p\n", &argv[0]);
	// kprintf("argv[0] now: %p\n", argv[0]);
	// kprintf("argv[0] now: %s\n", (char*) argv[0]);
	// kprintf("Value of argv:%p\n", argv);
	// kprintf("Address of argv[0]:%p\n",&argv[0]);
	/* Warp to user mode. */
		enter_new_process(nargs, (userptr_t) stackptr ,
				  stackptr, entrypoint);
	//enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	//		  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

}

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
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <uio.h>
#include <kern/iovec.h>
#include <process.h>
#include <synch.h>
#include <spl.h>
#include <addrspace.h>
#include <copyinout.h>
#include <filesupport.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <test.h>

/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */
void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int64_t retval64;
	bool retval_is_32 = true;	// Indicates if the return values is a 32 bit or 64 bit.
	int err;
	off_t arg64_1 = 0;			
	int whence = 0;

	//kprintf("Inside syscall.\n");
	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);
	//kprintf("Past kasserts.\n");

	callno = tf->tf_v0;

	if(callno == SYS_lseek) {
		// Create the 64-bit arguments, in case we need them.
		//kprintf("arg2 = %d, arg3 = %d.\n", tf->tf_a2, tf->tf_a3);
		//arg64_1 = ((off_t) tf->tf_a2) + ((off_t) tf->tf_a3 * 4294967296);
		// Need to OR and shift in order, since variables are only 32-bit.
		arg64_1 |= tf->tf_a2;
		arg64_1 = arg64_1 << 32;
		arg64_1 |= tf->tf_a3;
		// Copy in the whence argument from userland.
		err = copyin((const_userptr_t)(tf->tf_sp+16), &whence, sizeof(whence));
		if (err) {
			kprintf("Bad mem point.\n");
		}
		//kprintf("Whence = %d., arg61_1 = %ld.\n", whence, (long int)arg64_1);
	}

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	struct thread *cur = curthread;
	(void)cur;
	
	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	    case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0,
				 (userptr_t)tf->tf_a1);
		break;

		/*Open*/
		case SYS_open:
			err = sys_open((char*) tf->tf_a0, (int) tf->tf_a1, &retval);
		break;

		/*Write*/
		case SYS_write:
			err = sys_write((int) tf->tf_a0, (const void*) tf->tf_a1, (size_t) tf->tf_a2, &retval);
		break;

		/*Read*/
		case SYS_read:
			err = sys_read((int) tf->tf_a0, (const void*) tf->tf_a1, (size_t) tf->tf_a2, &retval);
			break;
		
		/*Close*/
		case SYS_close:
			err = sys_close((int) tf->tf_a0);
			break;

		/*lseek*/
		case SYS_lseek:
			err = sys_lseek((int) tf->tf_a0, (off_t) arg64_1, (int) whence, &retval64);
			retval_is_32 = false;
			break;

		/* FD redirect */
		case SYS_dup2:
			err = sys_dup2((int) tf->tf_a0, (int) tf->tf_a1, &retval);
			break;

		/* Change Directory */
		case SYS_chdir:
			err = sys_chdir((const char*) tf->tf_a0, &retval);
			break;

		/* Get CWD */
		case SYS___getcwd:
			err = sys___getcwd((char*) tf->tf_a0, (size_t) tf->tf_a1, &retval);
			break;

		case SYS_getpid:
			err = sys_getpid(&retval);
			break;

		case SYS__exit:
			sys_exit((int) tf->tf_a0);
			break;

		case SYS_waitpid:
			err = sys_waitpid((int) tf->tf_a0, (int*) tf->tf_a1, (int) tf->tf_a2, &retval); 	
 			break;

 		case SYS_fork:
 			err = sys_fork(tf, &retval);
 			break;

 		case SYS_execv:
 			err = sys_execv((const char*) tf->tf_a0, (char**) tf->tf_a1, &retval);
 			break;

	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      				/* signal an error */
	}
	else {
		/* Success. */
		if (retval_is_32){
			tf->tf_v0 = retval; 			/* retval, if appropriate for syscall */
		}
		else {
			tf->tf_v0 = retval64 >> 32;		// High order bits put in v0
			tf->tf_v1 = retval64;			// Low order bits in v1
		}
		tf->tf_a3 = 0;      				/* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked process.
 *
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
void
enter_forked_process(void *tf, unsigned long parentpid)
{
	// struct process *parent = get_process((pid_t) parentpid);
	// P(parent->p_forksem);
	as_activate(curthread->t_addrspace);
	(void)parentpid;
	struct trapframe newtf;
	struct trapframe *oldtf = tf;
	newtf.tf_vaddr = oldtf->tf_vaddr;	
	newtf.tf_status = oldtf->tf_status;	
	newtf.tf_cause = oldtf->tf_cause;	
	newtf.tf_lo = oldtf->tf_lo;
	newtf.tf_hi = oldtf->tf_hi;
	newtf.tf_ra = oldtf->tf_ra;		/* Saved register 31 */
	newtf.tf_at = oldtf->tf_at;		/* Saved register 1 (AT) */
	newtf.tf_v0 = oldtf->tf_v0;		/* Saved register 2 (v0) */
	newtf.tf_v1 = oldtf->tf_v1;		/* etc. */
	newtf.tf_a0 = oldtf->tf_a0;
	newtf.tf_a1 = oldtf->tf_a1;
	newtf.tf_a2 = oldtf->tf_a2;
	newtf.tf_a3 = oldtf->tf_a3;
	newtf.tf_t0 = oldtf->tf_t0;
	newtf.tf_t1 = oldtf->tf_t1;
	newtf.tf_t2 = oldtf->tf_t2;
	newtf.tf_t3 = oldtf->tf_t3;
	newtf.tf_t4 = oldtf->tf_t4;
	newtf.tf_t5 = oldtf->tf_t5;
	newtf.tf_t6 = oldtf->tf_t6;
	newtf.tf_t7 = oldtf->tf_t7;
	newtf.tf_s0 = oldtf->tf_s0;
	newtf.tf_s1 = oldtf->tf_s1;
	newtf.tf_s2 = oldtf->tf_s2;
	newtf.tf_s3 = oldtf->tf_s3;
	newtf.tf_s4 = oldtf->tf_s4;
	newtf.tf_s5 = oldtf->tf_s5;
	newtf.tf_s6 = oldtf->tf_s6;
	newtf.tf_s7 = oldtf->tf_s7;
	newtf.tf_t8 = oldtf->tf_t8;
	newtf.tf_t9 = oldtf->tf_t9;
	newtf.tf_k0 = oldtf->tf_k0;		/* dummy (see exception.S comments) */
	newtf.tf_k1 = oldtf->tf_k1;		/* dummy */
	newtf.tf_gp = oldtf->tf_gp;
	newtf.tf_sp = oldtf->tf_sp;
	newtf.tf_s8 = oldtf->tf_s8;
	newtf.tf_epc = oldtf->tf_epc;
	newtf.tf_epc += 4;
	newtf.tf_v0 = 0;
	newtf.tf_a3 = 0;
	kfree(tf);
	// memcpy(newtf, tf, sizeof(struct trapframe));
	// (void)junk;
	// kprintf("Entering Forked Process: %d\n",curthread->t_pid);
	// (void)parent;
	// newtf_tf_v0 = 0;
	// newtf_tf_a3 = 0;
	// newtf->tf_epc +=4;
	mips_usermode(&newtf);
}

static struct vnode* console;
static char conname[] = "con:";
// static const char* condevname = "con";

/* Initialization Functions */

/* Get the Console vnode once, so we dont't have to later */
int
console_init()
{
	int result;
	//Get the Console vnode
	// vfs_biglock_acquire();
	DEBUG(DB_WRITE,"Getting console vnode...");
	//path, openflags (O_WRONLY), mode (ignored b/c no permissions), vnode
	result = vfs_open(conname,O_WRONLY,0,&console);
	// vfs_biglock_release();
	KASSERT(console != NULL);
	KASSERT(result == 0);
	return result;

	/*Not sure if we can just keep reusing the static vnode console
	after we get it once. Nor do I have any clue on whether this vnode
	is thread-safe (it probably isn't, but who knows)
	So here is the code that uses vfs_root, in case we need it. Again
	not sure what is right but this seemed to work...

	//Get a Console vnode (CALL THIS AFTER vfs_open was already called)
	vfs_biglock_acquire();
	result = vfs_getroot(condevname, &console);
	kprintf("\n Tried to get the console again: %d", result);
	vfs_biglock_release();
	*/
}

/* The write() System call
 * Calls the following function:
 *
 *    vop_write       - Write data from uio to file at offset specified
 *                      in the uio, updating uio_resid to reflect the
 *                      amount written, and updating uio_offset to match.
 *                      Not allowed on directories or symlinks.
 * Per the man pages:
 * Each read (or write) operation is atomic relative to other I/O to the same file.
*/

int
sys_open(char* filename, int flags, int *retval)
{

	//kprintf("Inside sys_open\n");

	// Get current thread and current process
	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	// Get a file handle and file object pointers
	struct file_handle *fh;
	struct file_object *fo;
	int fd;
	int fo_index; // Index into file object array if it exits; -1 if no object and need to create.
	int result;

	// Find avaiable file descritpor.
	fd = get_free_file_descriptor(proc->p_id);
	if(fd < 0) {
		// fd table is full; no fd's available to allocate.		
		return EMFILE;
	}

	// Create file handle, with flags initialized; place pointer in process fd table.
	fh = fh_create(filename, flags);
	if(fh == NULL) {
		//kprintf("Failed to create file handle.\n");
		return EMFILE;
	}
	proc->p_fd_table[fd] = fh;

	// Check if file object exists already; link to it in file handle if it exists.
	fo_index = check_file_object_list(filename);
	if(fo_index < 0) {
		fo = fo_create(filename);
	}
	// If it doesnt exist (and CREATE was used), create a new file object; link to fh
	else {
		fo = file_object_list[fo_index];
	}
	if(fo == NULL) {
		return ENFILE;
	}
	fh->fh_file_object = fo;

	// Open/create vnode, which is the file. 
	result = vfs_open(filename, flags,/* Ignore MODE*/ 0, &fo->fo_vnode);
	if(result)
	{
		return EIO;
	}

	*retval = fd;
	// Successful sys_open, return file descriptor.
	return 0;
}

//0 stdin
//1 stdout
//2 stderr
int
sys_write(int fd, const void* buf, size_t nbytes, int* retval)
{
	//DEBUG(DB_THREADS, "\nCur thread name: %s\n",curthread->t_name);
	//DEBUG(DB_THREADS, "Cur thread info %p\n", curthread);
	//kprintf("\nParameter 1:%d",fd);
	//kprintf("\nParameter 2:%s", (char*) buf);
	//kprintf("\nParameter 3:%d", nbytes);

	///*
	
	//kprintf("File Descriptor is %d.\n", fd);
	//Can't write to Standard In
	
	if(fd == STDIN_FILENO)
	{
		//kprintf("File descriptor is %d.\n",fd);
		return EBADF;
	}
	

	// Check if buffer pointer is valid.
	if(buf == NULL)
	{
		//kprintf("Bad buffer pointer.\n");
		return EFAULT;
	}

	struct vnode* device;
	struct iovec iov;
	struct uio u;
	int result;
		struct thread *cur = curthread;
		struct process *proc = get_process(cur->t_pid);
		struct file_handle *fh = get_file_handle(proc->p_id, fd);


	//Write to Standard Out or Standard Err
	if(fd == STDOUT_FILENO || fd == STDERR_FILENO)
	{
		KASSERT(console != NULL);
		device = console;
		u.uio_offset = 0; //Start at the beginning
	}
	else 
	{

		struct file_object *fo = fh->fh_file_object;
		device = fo->fo_vnode;
		u.uio_offset = fh->fh_offset; //Start at the beginning
	}
	//TODO handle when fd is an actual file...

	//Create an iovec struct.
	iov.iov_ubase = (userptr_t) buf; //User pointer is the buffer
	iov.iov_len = nbytes; //The lengeth is the number of bytes passed in
	//Create a uio struct, too.
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; //Only 1 iovec (the one we created above)
	// u.uio_offset = 0; //Start at the beginning
	u.uio_resid = nbytes; //Amount of data remaining to transfer (all of it)
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE; //Operation Type: write 
	u.uio_space = curthread->t_addrspace; //Get address space from curthread (is this right?)
	
	//Pass this stuff to VOP_WRITE
	result = VOP_WRITE(device, &u);
	if(result)
	{
		//TODO check for nonzero error codes and return something else if needed...
		//return proper error code
		return EIO;
	}



	//resid amount of data remaining to transfer. nbytes the amount of data we need to write.
	//Our write succeeded. Per the man pages, return the # of bytes written (might not be everything)
	*retval = nbytes - u.uio_resid;


	if(fd == STDOUT_FILENO || fd == STDERR_FILENO)
	{
		return 0;
	}
	else
	{
		fh->fh_offset = u.uio_offset;
	}
	return 0;

}


int
sys_read(int fd, const void* buf, size_t buflen, int* retval)
{
	/*
	(void)fd;
	(void)buf;
	(void)buflen;
	(void)retval;
	*/

	//Variables and structs
	struct iovec iov;
	struct uio u;
	int result;
	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	struct file_object *fo = fh->fh_file_object;
	int bytes_read = 0;

	//Can't read from Standard Out or Standard Error
	//kprintf("Current fd is %d.\n",fd);
	/*
	if(fd == STDOUT_FILENO || STDERR_FILENO)
	{
		kprintf("Current fd is %d.\n",fd);
		kprintf("Cannot read from STDOUT or STDERR.\n");
		return EBADF;
	}
	*/
	//First check that the specified file is open for reading.
	if(!(fh->fh_flags & (O_RDONLY|O_RDWR)))
	{
		//kprintf("File is not open for reading.\n");
		return EBADF;
	}

	// Check that buffer pointer is valid.
	if(buf == NULL)
	{
		//kprintf("Buffer pointer is bad.\n");
		return EBADF;
	}

	// Initialize iov and uio
	iov.iov_ubase = (userptr_t) buf;		//User pointer is the buffer
	iov.iov_len = buflen; 					//The lengeth is the number of bytes passed in
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; 						//Only 1 iovec
	u.uio_offset = fh->fh_offset; 			//Start at the file handle offset
	u.uio_resid = buflen;					//Amount of data remaining to transfer
	u.uio_segflg = UIO_USERSPACE;			//
	u.uio_rw = UIO_READ; 					//Operation Type: read 
	u.uio_space = curthread->t_addrspace;	//Get address space from curthread (is this right?)
	
	result = VOP_READ(fo->fo_vnode, &u);
	if (result) {
		//kprintf("\nRead opreation failed.\n");
		return EIO;
	}

	// Determine bytes read.
	bytes_read = u.uio_offset - fh->fh_offset;
	// Update file offset in file handle.
	fh->fh_offset = u.uio_offset;

	*retval = bytes_read;

	return 0;
}

int
sys_close(int fd)
{
	//(void)filename;
	//kprintf("Inside sys_close\n");

	// Get current thread and current process
	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	// Get a file handle and file object pointers
	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	struct file_object *fo = fh->fh_file_object;

	// Check if fd is not a valid file descriptor.
	if(proc->p_fd_table[fd] == NULL) {
		//kprintf("File descriptor is not valid for closing.\n");
		return EBADF;
	}
	// Call vfs_close().
	vfs_close(fo->fo_vnode);

	// Decrement file handle reference count; if zero, destroy file handle.
	fh->fh_open_count -= 1;
	if(fh->fh_open_count == 0) {
		fh_destroy(fh);
	}

	// Free file descriptor; if 0, 1, or 2, reset file descriptor to STDIN, STDOUT, STDERR, respectively.
	release_file_descriptor(proc->p_id, fd);

	return 0;
}

int
sys_lseek(int fd, off_t pos, int whence, int64_t* retval64)
{
	//(void)fd;
	//(void)pos;
	//(void)whence;
	
	//kprintf("Inside lseek.\n");
	//kprintf("fd = %d.\n", fd);
	struct thread *cur = curthread;
	struct process *proc = get_process(cur->t_pid);
	if(proc->p_fd_table[fd] == NULL) {
		//kprintf("File descriptor is not valid for seeking.\n");
		return EBADF;
	}

	struct file_handle *fh = get_file_handle(proc->p_id, fd);
	struct file_object *fo = fh->fh_file_object;
	
	// Get the file stats so we can determine file size if needed.
	struct stat file_stat;
	int result;

	

	//kprintf("Past stat. Result %d. File size %ld.\n", result, (long int)file_stat.st_size);
	//kprintf("Whence = %d.\n", whence);
	switch (whence) {
		case SEEK_SET:
			// Beginning of file plus pos.
			fh->fh_offset = pos;
			result = 0;
			break;
		case SEEK_CUR:
			// Current offset plus pos.
			fh->fh_offset += pos;
			result = 0;
			break;
		case SEEK_END:
			// File size plus pos.
			result = VOP_STAT(fo->fo_vnode, &file_stat);
			fh->fh_offset = file_stat.st_size + pos;
			//fh->fh_offset = pos;
			result = 0;
			break;
		default:
			//kprintf("Seek whence is not valid.\n");
			return EINVAL;
			break;
	}
	//kprintf("pos = %ld.\n", (long int)pos);

	*retval64 = (int)fh->fh_offset;

	return 0;
}

int
sys_dup2(int oldfd, int newfd, int* retval)
{
	(void)oldfd;
	(void)newfd;
	*retval = 0;
	return 0;
}

int
sys_chdir(const char* pathname, int* retval)
{
	//(void)pathname;
	int result;
	char *cd = (char*)pathname;

	if(pathname == NULL) {
		// Pathname is an invalid pointer.
		return EFAULT;
	}
	result = vfs_chdir(cd);
	if(result) {
		// Hard I/O error.
		return EIO;
	}

	*retval = 0;
	return 0;

}

int
sys___getcwd(char* buf, size_t buflen, int* retval)
{
	//(void)buf;
	//(void)buflen;

	struct iovec iov;
	struct uio u;
	int result;
	//struct thread *cur = curthread;
	//struct process *proc = get_process(cur->t_pid);
	//struct file_handle *fh = get_file_handle(proc->p_id, fd);
	//struct file_object *fo = fh->fh_file_object;
	//int bytes_read = 0;

	// Initialize iov and uio
	iov.iov_ubase = (userptr_t) buf;		//User pointer is the buffer
	iov.iov_len = buflen; 					//The lengeth is the number of bytes passed in
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; 						//Only 1 iovec
	u.uio_offset = 0; 						//Start at the file handle offset
	u.uio_resid = buflen;					//Amount of data remaining to transfer
	u.uio_segflg = UIO_USERSPACE;			//User pointer
	u.uio_rw = UIO_READ; 					//Operation Type: read 
	u.uio_space = curthread->t_addrspace;	//Get address space from curthread (is this right?)
	
	result = vfs_getcwd(&u);
	if (result) {
		return EIO;
	}

	*retval = buflen - u.uio_resid;
	return 0;
}
int
sys_getpid(int* retval)
{
	//Sanity Check
	KASSERT(curthread != NULL);
	*retval = (int) curthread->t_pid;
	return 0;
}

void
sys_exit(int exitcode)
{
	// kprintf("Exit%d\n",curthread->t_pid);
	process_exit(curthread->t_pid,exitcode);
	return;
}

int
sys_waitpid(pid_t pid, int* status, int options, int* retval)
{
	pid_t curpid = curthread->t_pid; /*getpid()*/
	//We don't support any "options"
	if(options != 0)
	{
		return EINVAL;
	}
	//Check if status is null
	if(status == NULL)
	{
		return EFAULT;
	}
	//Check if status is an invalid pointer. If so, return err from copyin.
	int kstatus;// = status;
	int err = copyin((const_userptr_t) status, &kstatus,sizeof(int));
	if(err)
	{
		return err;
	}
	//If the process doesn't exist or pid is invalid:
	pidstate_t pidstate = get_pid_state(pid);
	if(pidstate == P_FREE || pidstate == P_INVALID)
	{
		return ESRCH;
	}
	//If we are not the parent
	if(get_process_parent(pid) != curpid)
	{
		return ECHILD;
	}
	kstatus = process_wait(curpid, pid);
	copyout(&kstatus, (userptr_t) status, sizeof(int));
	*retval = pid;
	return 0;
}

int
sys_fork(struct trapframe *tf, int* retval)
{
	// int x = splhigh();
	//Make a copy of the trapframe, so we can pass it to the child:
	struct trapframe *frame = kmalloc(sizeof(struct trapframe));
	if(frame == NULL)
	{
		//If frame is null, we're out of memory
		return ENOMEM;
	}
	memcpy(frame,tf,sizeof(struct trapframe));
	
	//Get the current process, so we can wait until fork is done.
	struct process *cur = get_process(curthread->t_pid);

	//Create the new process
	unsigned long curpid = (unsigned long) curthread->t_pid;
	struct process *newprocess = NULL;
	// splhigh();
	int result = thread_forkf(cur->p_name, enter_forked_process,(void*) frame,curpid,NULL,&newprocess);
	if(result)
	{
		kfree(frame);
		return result;
	}	
	//Wait until the child starts to return from fork.
	

	*retval = newprocess->p_id;
	// V(cur->p_forksem);

	// spl0();
	// splx(x);
	return 0;
}

/*
 * waitpid() system call, for use by the kernel ONLY.
 * This should be called !!!ONLY!!! from the kernel menu, as it does not check to see
 * if the status pointer passed in is safe or not.
 */
int
kern_sys_waitpid(pid_t pid, int* status, int options, int* retval)
{
	pid_t curpid = curthread->t_pid; /*getpid()*/
	//We don't support any "options"
	if(options != 0)
	{
		return EINVAL;
	}
	//Check if status is null
	if(status == NULL)
	{
		return EFAULT;
	}
	//If the process doesn't exist or pid is invalid:
	pidstate_t pidstate = get_pid_state(pid);
	if(pidstate == P_FREE || pidstate == P_INVALID)
	{
		return ESRCH;
	}
	//If we are not the parent
	if(get_process_parent(pid) != curpid)
	{
		return ECHILD;
	}
	*status = process_wait(curpid, pid);
	*retval = pid;
	return 0;
}

/* The exec() system call */
int
sys_execv(const char* program, char** args, int* retval)
{
	char kprogram[128]; (void) kprogram;
	// char* kprogram = (char*) kmalloc(128); (void) kprogram;
	int bytesCopied = 0;
	int result = 0;
	// char* usrprogram = (char*) program; (void) usrprogram;
	// const_userptr_t usrprogram = (const_userptr_t) program; (void) usrprogram;
	char curchar = '\0';
	do {
		result = copyin((userptr_t) program,&curchar,sizeof(char));
		if(result)
		{
			return result;
		}
		kprogram[bytesCopied] = curchar;
		bytesCopied++;
		program = program+1; 

	} while(curchar != '\0');
	// kprintf("Copied in program\n");
	int count = 0;
	bytesCopied = 0;

	int max = 1024 * 4;
	(void)max;
	userptr_t curusrarg;
	char* curarg;

	// kprintf("args0:%s\n",args[0]);

	char* buf = (char*) kmalloc(max);
	while(args[count] != NULL)
	{
		//Copy in pointer to current arg..
		curusrarg = (userptr_t) args[count];
		result = copyin(curusrarg, (void*) curarg, 4);
		if(result)
		{
			return result;
		}
		//Copy in the arg, char by char.
		do
		{	
			if(bytesCopied == max)
			{
				return E2BIG;
			}
			result = copyin(curusrarg, &curchar, sizeof(char));
			if(result)
			{
				return result;
			}
			buf[bytesCopied] = curchar;
			bytesCopied++;
			curusrarg++;
		} while(curchar != '\0');
		//pad the arg, if needed
		while(bytesCopied % 4 != 0)
		{
			buf[bytesCopied] = '\0';
			bytesCopied++;
		}
		count++;

	}

	// for(int i=0;i<bytesCopied;i++)
	// {	
	// 	kprintf("%s\n", (char *) &buf[i]);
	// }

	char* kargs[count+1];
	int pos = 0;
	kargs[count] = NULL;
	for(int j=0;j<count;j++)
	{
		kargs[j] = (char*) &buf[pos];
		while(buf[pos] != '\0')
		{
			pos++;
		}
	}
	// kprintf("kargs[0]:%s\n", (char*) kargs[0]);
	// kprintf("kargs[1]:%s\n", (char*) kargs[1]);
	kfree(buf);
	runprogram2(&kprogram[0], kargs,count);
	(void)program;
	(void)args;
	(void)retval;
	return 0;
}

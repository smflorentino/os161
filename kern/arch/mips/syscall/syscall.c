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
	int err;

	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);

	callno = tf->tf_v0;

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
			err = sys_open((const char*) tf->tf_a0, (int) tf->tf_a1);
		break;

		/*Write*/
		case SYS_write:
			err = sys_write((int) tf->tf_a0, (const void*) tf->tf_a1, (size_t) tf->tf_a2, &retval);
		break;

		/*Read*/
		case SYS_read:
			err = sys_read((int) tf->tf_a0, (const void*) tf->tf_a1, (size_t) tf->tf_a2, &retval);
			break;
		
		case SYS_getpid:
			err = sys_getpid(&retval);
			break;

		case SYS__exit:
			sys_exit((int) tf->tf_a0);
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
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval; /* retval, if appropriate for syscall */
		tf->tf_a3 = 0;      /* signal no error */
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
enter_forked_process(struct trapframe *tf)
{
	(void)tf;
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
sys_open(const char* filename, int flags)
{
	(void)filename;
	(void)flags;
	kprintf("Inside sys_open\n");

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

	//Can't write to Standard In
	//TODO...need more data cleansing here...
	if(fd == STDIN_FILENO)
	{
		return EBADF;
	}
	if(buf == NULL)
	{
		return EFAULT;
	}

	//Declare stuff we need
	struct vnode* device;
	struct iovec iov;
	struct uio u;
	int result;

	//Write to Standard Out or Standard Err
	if(fd == STDOUT_FILENO || fd == STDERR_FILENO)
	{
		KASSERT(console != NULL);
		device = console;
	}
	//TODO handle when fd is an actual file...

	//Create an iovec struct.
	iov.iov_ubase = (userptr_t) buf; //User pointer is the buffer
	iov.iov_len = nbytes; //The lengeth is the number of bytes passed in
	//Create a uio struct, too.
	u.uio_iov = &iov; 
	u.uio_iovcnt = 1; //Only 1 iovec (the one we created above)
	u.uio_offset = 0; //Start at the beginning
	u.uio_resid = nbytes; //Amount of data remaining to transfer (all of it)
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE; //Operation Type: write 
	u.uio_space = curthread->t_addrspace; //Get address space from curthread (is this right?)
	
	//Pass this stuff to VOP_WRITE
	result = VOP_WRITE(device, &u);
	if(result != 0)
	{
		//TODO check for nonzero error codes and return something else if needed...
		//return proper error code
		return -1;
	}

	//resid amount of data remaining to transfer. nbytes the amount of data we need to write.
	//Our write succeeded. Per the man pages, return the # of bytes written (might not be everything)
	*retval = nbytes - u.uio_resid;
	return 0;
}


int
sys_read(int fd, const void* buf, size_t buflen, int* retval)
{

	(void)fd;
	(void)buf;
	(void)buflen;
	(void)retval;

	//Variables
	/*
	int result;

	*/

	//Can't read to Standard Out or Standard Error
	//TODO...need more data cleansing here...
	if(fd == STDOUT_FILENO || STDERR_FILENO)
	{
		return EBADF;
	}

	//First check that the specified file is open for reading.
	//Need to build Filehandle first; part of file handle will indicate status of file:
	// Read, Write, RW, etc.
	/*
	if(!R or RW)
	{
		return EBADF;
	}
	*/

	//Second, check that the buffer pointed to is valid.
	/*
	if(Buffer is invalid)
	{
		return EFAULT;
	}
	*/

	//result = VOP_READ();






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
	kprintf("Exiting Process %d\n",curthread->t_pid);
	struct thread *cur = curthread;
	process_exit(cur->t_pid,exitcode);
	(void) exitcode;
	return;
}

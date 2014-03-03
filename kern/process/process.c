/*
	A Userland Process
*/

#include <types.h>
#include <lib.h>
#include <limits.h>
#include <synch.h>
#include <current.h>
#include <process.h>
#include <processlist.h>
#include <filesupport.h>

/*Max PID is 32767, Min PID is 2*/

/* A Table of Processes*/
static struct process *processtable[PID_MAX + 1];
/* A Table of Process IDs*/
static int freepidlist[PID_MAX + 1];
/* A Table of Waiters on a particular Process ID */
static int waitercountpidlist[PID_MAX + 1];
/* A Table of Exit Codes */
static int exitcodelist[PID_MAX + 1];
/* Lock for the Process Table IDs*/
static struct lock *processtable_biglock;

/* Create a new process, and add it to the process table*/
struct process *
process_create(const char *name)
{
	struct process *process;

	process = kmalloc(sizeof(struct process));
	if(process == NULL) {
		//TODO probably need to fix this.
		return NULL;
	}

	process->p_name = kstrdup(name);
	if(process->p_name == NULL) {
		kfree(process);
		return NULL;
	}

	process->p_waitcv = cv_create(name);
	if(process->p_waitcv == NULL) {
		kfree(process->p_name);
		kfree(process);
		return NULL;
	}

	process->p_waitlock = lock_create(name);
	if(process->p_waitlock == NULL) {
		cv_destroy(process->p_waitcv);
		kfree(process->p_name);
		kfree(process);
	}

	// processlist_init(&process->p_waiters);
	// if(&process->p_waiters == NULL) {
	// 	lock_destroy(process->p_waitlock);
	// 	cv_destroy(process->p_waitcv);
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }

	process->p_id = allocate_pid();
	processtable[process->p_id] = process;
	return process;
}

/* Should only be called once, by the kernel*/
struct process *
init_process_create(const char *name)
{
	struct process *process;

	process = kmalloc(sizeof(struct process));
	if(process == NULL) {
		//TODO probably need to fix this.
		return NULL;
	}

	process->p_name = kstrdup(name);
	if(process->p_name == NULL) {
		kfree(process);
		return NULL;
	}

	process->p_waitcv = cv_create(name);
	if(process->p_waitcv == NULL) {
		kfree(process->p_name);
		kfree(process);
		return NULL;
	}

	process->p_waitlock = lock_create(name);
	if(process->p_waitlock == NULL) {
		cv_destroy(process->p_waitcv);
		kfree(process->p_name);
		kfree(process);
	}

	// //processlist_init(&process->p_waiters);
	// if(&process->p_waiters == NULL) {
	// 	lock_destroy(process->p_waitlock);
	// 	cv_destroy(process->p_waitcv);
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }

	curthread->t_pid = 1;
	process->p_id = 1;
	processtable[process->p_id] = process;
	return process;
}

/* Called by exit() */
void
process_exit(pid_t pid, int exitcode)
{
	//Register the process exit code
	int index = (int) pid;
	exitcodelist[index] = exitcode;

	struct process *process = get_process(pid);

	/* Wake up anyone listening*/
	lock_acquire(process->p_waitlock);
	cv_broadcast(process->p_waitcv, process->p_waitlock);
	lock_release(process->p_waitlock);
	//Clean up later
	thread_exit();
}

void
process_destroy(pid_t pid)
{
	struct process *process = get_process(pid);
	release_pid(pid);
	kfree(process->p_name);
	lock_destroy(process->p_waitlock);
	cv_destroy(process->p_waitcv);
	//processlist_cleanup(&process->p_waiters);
	kfree(process);
}

struct process *
get_process(pid_t pid)
{
	int index = (int) pid;
	return processtable[index];
}

int
get_process_exitcode(pid_t pid)
{
	int index = (int) pid;
	return exitcodelist[index];
}

/* Called by waitpid() */
void
process_wait(pid_t pidToWait, pid_t pidToWaitFor)
{
	// struct process *waiter = get_process(pidToWait);
	(void)pidToWait;
	struct process *notifier = get_process(pidToWaitFor);

	/* Let notifier know to wake up the parent when leaving */
	//processlist_addhead(&notifier->p_waiters,waiter);
	/* Update the waiter count for the process we're going to wait for*/
	increment_waiter_count(pidToWaitFor);

	lock_acquire(notifier->p_waitlock);
	
	cv_wait(notifier->p_waitcv,notifier->p_waitlock);
	/* We're no longer waiting. Decrement the waiter count for the process we
	were waiting for */
	decrement_waiter_count(pidToWaitFor);
	lock_release(notifier->p_waitlock);
	if(waitercountpidlist[pidToWaitFor] == 0)
	{
		//Clean up the zombie process, now
		process_destroy(pidToWaitFor);	
	}

}

/*
	Allocate a process ID for somebody. This is relatively simple. We just
	loop through the array until we find a PID marked free. Then we allocate that one.
*/
int
allocate_pid()
{
	processtable_biglock_acquire();
	for(int i = PID_MIN;i<=PID_MAX;i++)
	{
		//found a free PID
		if(freepidlist[i] == 0)
		{
			freepidlist[i] = 1;
			processtable_biglock_release();
			return i;
		}
	}
	processtable_biglock_release();
	//Do we really have to panic? Probably not. More likely we need to return an error code to somebody.
	//But this will have to do for now. 
	panic("No more available process IDs!");
	return -1;
}

/* Free a Process ID. This should be called when a process is destroyed */
void
release_pid(int pidToFree)
{
	KASSERT(pidToFree >= PID_MIN);
	processtable_biglock_acquire();
	freepidlist[pidToFree] = 0;
	processtable_biglock_release();
}

/* When a process calls waitpid(), we call this */
//static
void
increment_waiter_count(pid_t pidToWaitFor)
{
	processtable_biglock_acquire();
	waitercountpidlist[pidToWaitFor]++;
	processtable_biglock_release();
}

/* Called when a process wakes up or gets terminated */
//static
void
decrement_waiter_count(pid_t pidToWaitFor)
{
	processtable_biglock_acquire();
	waitercountpidlist[pidToWaitFor]--;
	//Sanity Check
	KASSERT(waitercountpidlist[pidToWaitFor] >= 0);
	processtable_biglock_release();
}



/*
	Initialize our process ID list. A 0 indicates free, a 1 indicates used.
	Note that we respect PID_MIN and PID_MAX here.
*/
void
processtable_bootstrap()
{
	//Processes up to PID_MIN are not allowed.
	for(int i=0;i<PID_MIN;i++)
	{
		freepidlist[i] = 1;
	}
	//But everything else is.
	for(int i = PID_MIN;i<= PID_MAX;i++)
	{
		freepidlist[i] = 0;
	}

	/* Initialize the waiter table. Any process that has another process
	waiting for it will have its counter incremented here. We'll use the 
	processtable_biglock for simplicty
	Also initialize the exit code table to be 0, for now */
	for(int i = 0;i <= PID_MAX;i++)
	{
		waitercountpidlist[i] = 0;
		exitcodelist[i] = 0;
	}

	processtable_biglock = lock_create("process lock");
}

void
processtable_biglock_acquire()
{
	//Let's panic for now if we already have the lock
	//Because I don't know if if this will ever happen anyway.
	KASSERT(!lock_do_i_hold(processtable_biglock)); 
	lock_acquire(processtable_biglock);
}

void
processtable_biglock_release()
{
	KASSERT(lock_do_i_hold(processtable_biglock));
	lock_release(processtable_biglock);
}

bool
processtable_biglock_do_i_hold()
{
	return lock_do_i_hold(processtable_biglock);
}

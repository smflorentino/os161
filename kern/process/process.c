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

/*Max PID is 32767, Min PID is 2*/

/* A Table of Processes*/
static struct process *processtable[PID_MAX + 1];
/* A Table of Process IDs*/
static int freepidlist[PID_MAX + 1];
/* A Table of Parent processes */
static int parentprocesslist[PID_MAX + 1];
/* A Table of Waiters on a particular Process ID */
// static int waitercountpidlist[PID_MAX + 1];
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

	// process->p_waitcv = cv_create(name);
	// if(process->p_waitcv == NULL) {
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }

	// process->p_waitlock = lock_create(name);
	// if(process->p_waitlock == NULL) {
	// 	cv_destroy(process->p_waitcv);
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }

	process->p_waitsem = sem_create(name,0);
	if(process->p_waitsem == NULL) {
		kfree(process->p_name);
		kfree(process);
		return NULL;
	}

	process->p_forksem = sem_create(name,0);
	if(process->p_forksem == NULL) {
		// lock_destroy(process->p_waitlock);
		// cv_destroy(process->p_waitcv);
		kfree(process->p_waitsem);
		kfree(process->p_name);
		kfree(process);
		return NULL;
	}

	// processlist_init(&process->p_waiters);
	// if(&process->p_waiters == NULL) {
	// 	lock_destroy(process->p_waitlock);
	// 	cv_destroy(process->p_waitcv);
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }
	processtable_biglock_acquire();
	process->p_id = allocate_pid();
	processtable[process->p_id] = process;
	processtable_biglock_release();
	// kprintf("=Created%d=\n",process->p_id);
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

	// process->p_waitcv = cv_create(name);
	// if(process->p_waitcv == NULL) {
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }

	// process->p_waitlock = lock_create(name);
	// if(process->p_waitlock == NULL) {
	// 	cv_destroy(process->p_waitcv);
	// 	kfree(process->p_name);
	// 	kfree(process);
	// }

	// //processlist_init(&process->p_waiters);
	// if(&process->p_waiters == NULL) {
	// 	lock_destroy(process->p_waitlock);
	// 	cv_destroy(process->p_waitcv);
	// 	kfree(process->p_name);
	// 	kfree(process);
	// 	return NULL;
	// }
	process->p_waitsem = sem_create(name,0);
	if(process->p_waitsem == NULL) {
		kfree(process->p_name);
		kfree(process);
		return NULL;
	}

	process->p_forksem = sem_create(name,0);
	if(process->p_forksem == NULL) {
		// lock_destroy(process->p_waitlock);
		// cv_destroy(process->p_waitcv);
		kfree(process->p_waitsem);
		kfree(process->p_name);
		kfree(process);
		return NULL;
	}

	processtable_biglock_acquire();
	process->p_id = allocate_pid();
	processtable[process->p_id] = process;
	processtable_biglock_release();

	curthread->t_pid = process->p_id;
	return process;
}

/* Called by exit() */
void
process_exit(pid_t pid, int exitcode)
{
	// kprintf("==Exit%d",pid);
	processtable_biglock_acquire();
	/*If I have children, abandon them*/
	abandon_children(pid);
	/*Store the exit code*/
	exitcodelist[pid] = exitcode;
	/* Get my parent, if it's still alive*/
	if(parentprocesslist[pid] != 2)
	{
		/*Get the process that is exiting*/
		struct process *process = get_process(pid);
		/*Wake up anyone listening*/
		V(process->p_waitsem);		
	}
	/*Notify any future waitpid() calls to return immediately*/
	freepidlist[pid] = 2;
	processtable_biglock_release();
	
	/* Clean up when parent exits */
	thread_exit();
}

/* Called by waitpid() */
int
process_wait(pid_t pidToWait, pid_t pidToWaitFor)
{
	(void)pidToWait;
	int exitCode;
	processtable_biglock_acquire();
	struct process *notifier = get_process(pidToWaitFor);
	if(freepidlist[pidToWaitFor] == 2)
	{
		//The process we're waiting for already exited.
		//Return to sys_waitpid to collect the exit code.
		exitCode = exitcodelist[pidToWaitFor];
		processtable_biglock_release();
	}
	else
	{
		processtable_biglock_release();
		P(notifier->p_waitsem);
			exitCode = exitcodelist[pidToWaitFor];
	}
	process_destroy(notifier->p_id);
	return exitCode;
	/* Let notifier know to wake up the parent when leaving */
	//processlist_addhead(&notifier->p_waiters,waiter);
	/* Update the waiter count for the process we're going to wait for*/
	// increment_waiter_count(pidToWaitFor);

	// lock_acquire(notifier->p_waitlock);
	// cv_wait(notifier->p_waitcv,notifier->p_waitlock);
	/* We're no longer waiting. Decrement the waiter count for the process we
	were waiting for */
	// decrement_waiter_count(pidToWaitFor);
	// // lock_release(notifier->p_waitlock);
	// if(waitercountpidlist[pidToWaitFor] == 0)
	// {
	// 	//Clean up the zombie process, now
	// 	process_destroy(pidToWaitFor);	
	// }
}

void
process_destroy(pid_t pid)
{
	processtable_biglock_acquire();

	KASSERT(processtable_biglock_do_i_hold());
	struct process *process = get_process(pid);
	release_pid(pid);
	kfree(process->p_name);
	// lock_destroy(process->p_waitlock);
	sem_destroy(process->p_waitsem);
	sem_destroy(process->p_forksem);
	// cv_destroy(process->p_waitcv);
	//processlist_cleanup(&process->p_waiters);
	kfree(process);
	int num = (int) pid;
	processtable[num] = NULL;
	parentprocesslist[pid] = -1;
	processtable_biglock_release();
}

struct process *
get_process(pid_t pid)
{
	// processtable_biglock_acquire();
	int index = (int) pid;
	//Make sure the process exists:
	KASSERT(freepidlist[index] > 0);
	struct process *process = processtable[index];
	// processtable_biglock_release();
	KASSERT(process->p_id > 0 && process->p_id <PID_MAX);
	return process;
}

int
get_process_exitcode(pid_t pid)
{
	int index = (int) pid;
	return exitcodelist[index];
}

/*
	Allocate a process ID for somebody. This is relatively simple. We just
	loop through the array until we find a PID marked free. Then we allocate that one.
*/
int
allocate_pid()
{
	collect_children();
	for(int i = PID_MIN;i<=PID_MAX;i++)
	{
		//found a free PID
		if(freepidlist[i] == 0)
		{
			freepidlist[i] = 1;
			return i;
		}
	}
	//Do we really have to panic? Probably not. More likely we need to return an error code to somebody.
	//But this will have to do for now. 
	panic("No more available process IDs!");
	return -1;
}

/* Free a Process ID. This should be called when a process is destroyed */
void
release_pid(int pidToFree)
{
	(void)pidToFree;
	KASSERT(pidToFree >= PID_MIN);
	// processtable_biglock_acquire();
	freepidlist[pidToFree] = 0;
	// processtable_biglock_release();
}

void
set_process_parent(pid_t process, pid_t parent)
{
	processtable_biglock_acquire();
	parentprocesslist[process] = parent;
	processtable_biglock_release();
}

void
abandon_children(pid_t dyingParent)
{
	for(int i=0;i<=PID_MAX;i++)
	{
		if(parentprocesslist[i] == dyingParent)
		{
			//Assign parent to be init...
			parentprocesslist[i] = 2;
		}
	}
}

void collect_children()
{
	for(int i=PID_MIN;i<=PID_MAX;i++)
	{
		if(parentprocesslist[i] == 2)
		{
			int pid = i;
			KASSERT(processtable_biglock_do_i_hold());
			struct process *process = get_process(pid);
			release_pid(pid);
			kfree(process->p_name);
			// lock_destroy(process->p_waitlock);
			sem_destroy(process->p_waitsem);
			sem_destroy(process->p_forksem);
			// cv_destroy(process->p_waitcv);
			//processlist_cleanup(&process->p_waiters);
			kfree(process);
			int num = (int) pid;
			processtable[num] = NULL;
		}
	}	
}

// /* When a process calls waitpid(), we call this */
// //static
// void
// increment_waiter_count(pid_t pidToWaitFor)
// {
// 	processtable_biglock_acquire();
// 	waitercountpidlist[pidToWaitFor]++;
// 	processtable_biglock_release();
// }

//  Called when a process wakes up or gets terminated 
// //static
// void
// decrement_waiter_count(pid_t pidToWaitFor)
// {
// 	processtable_biglock_acquire();
// 	waitercountpidlist[pidToWaitFor]--;
// 	//Sanity Check
// 	KASSERT(waitercountpidlist[pidToWaitFor] >= 0);
// 	processtable_biglock_release();
// }



/*
	Initialize our process ID list. A 0 indicates free, a 1 indicates used.
	Note that we respect PID_MIN and PID_MAX here.
*/
void
processtable_bootstrap()
{
	//Set NULL entries in the process table:
	for(int i=0;i<=PID_MAX;i++)
	{
		parentprocesslist[i] = -1;
		processtable[i] = NULL;
	}
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

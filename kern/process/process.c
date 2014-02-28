/*
	A Userland Process
*/

#include <types.h>
#include <lib.h>
#include <limits.h>
#include <synch.h>
#include <process.h>
#include <processlist.h>

/*Max PID is 32767, Min PID is 2*/

/* A Table of Processes*/
static struct process *processtable[PID_MAX + 1];
/* A Table of Process IDs*/
static int freepidlist[PID_MAX + 1];
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

	process->p_id = allocate_pid();
	processtable[process->p_id] = process;
	return process;
}

void
process_exit(pid_t pid, int exitcode)
{
	(void)exitcode;
	struct process *process = get_process(pid);
	release_pid(pid);
	kfree(process->p_name);
	kfree(process);
	thread_exit();
}

void
process_destroy(pid_t pid)
{
	(void)pid;
}

struct process *
get_process(pid_t pid)
{
	int index = (int) pid;
	return processtable[index];
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
	processtable_biglock_acquire();
	freepidlist[pidToFree] = 0;
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

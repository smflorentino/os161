/*
	Kernel-Level Process System
*/
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <processlist.h>


static int freepidlist[PID_MAX + 1];
static struct lock *processlist_biglock;

/*
	Initialize our process ID list. A 0 indicates free, a 1 indicates used.
	Note that we respect PID_MIN and PID_MAX here.
*/
void
processlist_bootstrap()
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

	processlist_biglock = lock_create("process lock");
}

void
processlist_biglock_acquire()
{
	//Let's panic for now if we already have the lock
	//Because I don't know if if this will ever happen anyway.
	KASSERT(!lock_do_i_hold(processlist_biglock)); 
	lock_acquire(processlist_biglock);
}

void
processlist_biglock_release()
{
	KASSERT(lock_do_i_hold(processlist_biglock));
	lock_release(processlist_biglock);
}

bool
processlist_biglock_do_i_hold()
{
	return lock_do_i_hold(processlist_biglock);
}

/*
	Allocate a process ID for somebody. This is relatively simple. We just
	loop through the array until we find a PID marked free. Then we allocate that one.
*/
int
allocate_pid()
{
	processlist_biglock_acquire();
	for(int i = PID_MIN;i<=PID_MAX;i++)
	{
		//found a free PID
		if(freepidlist[i] == 0)
		{
			freepidlist[i] = 1;
			processlist_biglock_release();
			return i;
		}
	}
	processlist_biglock_release();
	//Do we really have to panic? Probably not. More likely we need to return an error code to somebody.
	//But this will have to do for now. 
	panic("No more available process IDs!");
	return -1;
}

/* Free a Process ID. This should be called when a process is destroyed */
void
release_pid(int pidToFree)
{
	processlist_biglock_acquire();
	freepidlist[pidToFree] = 0;
	processlist_biglock_release();
}

struct process *
get_process(pid_t pid)
{
	int index = (int) pid;
	return processlist[index];
}

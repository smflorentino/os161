/*
	A Userland Process
*/

#include <types.h>
#include <lib.h>
#include <process.h>
#include <processlist.h>

/* Create a new process, and add it to the processlist*/
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
	processlist[process->p_id] = process;
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

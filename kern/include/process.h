/* Process Header File */

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <types.h>
#include <thread.h>
#include <threadlist.h>

/* Process Structure */
struct process {
	char *p_name;
	pid_t p_id;

	struct process *p_parentprocess;
	struct thread *p_thread;
};

struct process *process_create(const char*);

void process_exit(pid_t pid, int exitcode);
void process_destroy(pid_t pid);

#endif /* _PROCESS_H_ */

/* Process Header File */

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <types.h>
#include <thread.h>
#include <processlist.h>

/* Process Structure */
struct process {
	char *p_name;
	pid_t p_id;

	/* Internal Stuff */
	struct processlistnode p_listnode; /* Link for run/sleep/zombie lists */

	struct process *p_parentprocess;
	struct thread *p_thread;
};

struct process *process_create(const char*);

void process_exit(pid_t pid, int exitcode);
void process_destroy(pid_t pid);

/* Call once during system startup to allocate data structures */
void processtable_bootstrap(void);

void processtable_biglock_acquire(void);
void processtable_biglock_release(void);
bool processtable_biglock_do_i_hold(void);

int allocate_pid(void);
void release_pid(int);

struct process* get_process(pid_t);

#endif /* _PROCESS_H_ */

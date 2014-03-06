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

	/*For waitpid()*/
	struct semaphore *p_waitsem;
	// struct cv *p_waitcv;
	// struct lock *p_waitlock;
	//struct processlist p_waiters;

	pid_t p_parentpid;
	//struct processlist p_children;
	struct thread *p_thread;

	/* For fork() */
	struct semaphore *p_forksem;
};
struct process *init_process_create(const char*);
struct process *process_create(const char*);

void process_exit(pid_t pid, int exitcode);
void process_destroy(pid_t pid);
int process_wait(pid_t pidToWait, pid_t pidToWaitFor);
/* Call once during system startup to allocate data structures */
void processtable_bootstrap(void);

void processtable_biglock_acquire(void);
void processtable_biglock_release(void);
bool processtable_biglock_do_i_hold(void);

int allocate_pid(void);
void release_pid(int);

void increment_waiter_count(pid_t);
void decrement_waiter_count(pid_t);
int get_waiter_count(pid_t);

struct process* get_process(pid_t);

int get_process_exitcode(pid_t);
void set_process_parent(pid_t,pid_t);
void abandon_children(pid_t);
void collect_children(void);

#endif /* _PROCESS_H_ */

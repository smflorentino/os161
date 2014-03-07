/* Process Header File */

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <types.h>
#include <thread.h>
#include <processlist.h>
#include <limits.h>

/* Process Structure */
struct process {
	char *p_name;
	pid_t p_id;

	/* Internal Stuff */
	struct processlistnode p_listnode; /* Link for run/sleep/zombie lists */

	/*For waitpid()*/
	struct cv *p_waitcv;
	struct lock *p_waitlock;
	//struct processlist p_waiters;

	pid_t p_parentpid;
	//struct processlist p_children;
	struct thread *p_thread;
	// Array of file handle pointers; initialize to NULL pointers on process creation.
	struct file_handle* p_fd_table[10/*OPEN_MAX*/];
};
struct process *init_process_create(const char*);
struct process *process_create(const char*);

void process_exit(pid_t pid, int exitcode);
void process_destroy(pid_t pid);
void process_wait(pid_t pidToWait, pid_t pidToWaitFor);
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
int get_free_file_descriptor(pid_t);	// Given a process id, returns a file descriptor that is free.
struct file_handle* get_file_handle(pid_t pid, int fd);	// Given a file descriptor, returns the pointer to the associated file handle.

int get_process_exitcode(pid_t);

#endif /* _PROCESS_H_ */

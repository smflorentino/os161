/* File to keep track of the list of running processes on the system */

#ifndef _PROCESSLIST_H_
#define _PROCESSLIST_H_

#include <limits.h>

struct process; /* from <process.h> */

//Max PID is 32767, Min PID is 2.
struct process *processlist[PID_MAX + 1];

/* Call once during system startup to allocate data structures */
void processlist_bootstrap(void);

void processlist_biglock_acquire(void);
void processlist_biglock_release(void);
bool processlist_biglock_do_i_hold(void);

int allocate_pid(void);
void release_pid(int);

struct process* get_process(pid_t);

#endif /* _PROCESSLIST_H_ */

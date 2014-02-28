/* File to keep track of the list of running processes on the system */

#ifndef _PROCESSLIST_H_
#define _PROCESSLIST_H_

struct process;	/* from <process.h> */

/*
 * AmigaOS-style linked list of processs.
 *
 * The two processlistnodes in the processlist structure are always on
 * the list, as bookends; this removes all the special cases in the
 * list handling code. However, note how THREADLIST_FOREACH works: you
 * iterate by starting with pl_head.pln_next, and stop when
 * itervar->pln_next is null, not when itervar itself becomes null.
 *
 * ->pln_self always points to the process that contains the
 * processlistnode. We could avoid this if we wanted to instead use
 *
 *    (struct process *)((char *)node - offsetof(struct process, t_listnode))
 *
 * to get the process pointer. But that's gross.
 */

struct processlistnode {
	struct processlistnode *pln_prev;
	struct processlistnode *pln_next;
	struct process *pln_self;
};

struct processlist {
	struct processlistnode pl_head;
	struct processlistnode pl_tail;
	unsigned pl_count;
};

/* Initialize and clean up a process list node. */
void processlistnode_init(struct processlistnode *pln, struct process *self);
void processlistnode_cleanup(struct processlistnode *pln);

/* Initialize and clean up a process list. Must be empty at cleanup. */
void processlist_init(struct processlist *pl);
void processlist_cleanup(struct processlist *pl);

/* Check if it's empty */
bool processlist_isempty(struct processlist *pl);

/* Add and remove: at ends */
void processlist_addhead(struct processlist *pl, struct process *t);
void processlist_addtail(struct processlist *pl, struct process *t);
struct process *processlist_remhead(struct processlist *pl);
struct process *processlist_remtail(struct processlist *pl);

/* Add and remove: in middle. (TL is needed to maintain ->pl_count.) */
void processlist_insertafter(struct processlist *pl,
			    struct process *onlist, struct process *addee);
void processlist_insertbefore(struct processlist *pl,
			     struct process *addee, struct process *onlist);
void processlist_remove(struct processlist *pl, struct process *t);

/* Iteration; itervar should previously be declared as (struct process *) */
#define PROCESSLIST_FORALL(itervar, pl) \
	for ((itervar) = (pl).pl_head.pln_next->pln_self; \
	     (itervar)->t_listnode.pln_next != NULL; \
	     (itervar) = (itervar)->t_listnode.pln_next->pln_self)

#define PROCESSLIST_FORALL_REV(itervar, pl) \
	for ((itervar) = (pl).pl_tail.pln_prev->pln_self; \
	     (itervar)->t_listnode.pln_prev != NULL; \
	     (itervar) = (itervar)->t_listnode.pln_prev->pln_self)


#endif /* _PROCESSLIST_H_ */

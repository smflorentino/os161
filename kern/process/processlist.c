/*
	Kernel-Level Process System
*/
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <process.h>
#include <processlist.h>

void
processlistnode_init(struct processlistnode *pln, struct process *t)
{
	DEBUGASSERT(pln != NULL);
	KASSERT(t != NULL);

	pln->pln_next = NULL;
	pln->pln_prev = NULL;
	pln->pln_self = t;
}

void
processlistnode_cleanup(struct processlistnode *pln)
{
	DEBUGASSERT(pln != NULL);

	KASSERT(pln->pln_next == NULL);
	KASSERT(pln->pln_prev == NULL);
	KASSERT(pln->pln_self != NULL);
}

void
processlist_init(struct processlist *pl)
{
	DEBUGASSERT(pl != NULL);

	pl->pl_head.pln_next = &pl->pl_tail;
	pl->pl_head.pln_prev = NULL;
	pl->pl_tail.pln_next = NULL;
	pl->pl_tail.pln_prev = &pl->pl_head;
	pl->pl_head.pln_self = NULL;
	pl->pl_tail.pln_self = NULL;
	pl->pl_count = 0;
}

void
processlist_cleanup(struct processlist *pl)
{
	DEBUGASSERT(pl != NULL);
	DEBUGASSERT(pl->pl_head.pln_next == &pl->pl_tail);
	DEBUGASSERT(pl->pl_head.pln_prev == NULL);
	DEBUGASSERT(pl->pl_tail.pln_next == NULL);
	DEBUGASSERT(pl->pl_tail.pln_prev == &pl->pl_head);
	DEBUGASSERT(pl->pl_head.pln_self == NULL);
	DEBUGASSERT(pl->pl_tail.pln_self == NULL);

	KASSERT(processlist_isempty(pl));
	KASSERT(pl->pl_count == 0);

	/* nothing (else) to do */
}

bool
processlist_isempty(struct processlist *pl)
{
	DEBUGASSERT(pl != NULL);

	return (pl->pl_count == 0);
}

////////////////////////////////////////////////////////////
// internal

/*
 * Do insertion. Doesn't update pl_count.
 */
static
void
processlist_insertafternode(struct processlistnode *onlist, struct process *t)
{
	struct processlistnode *addee;

	addee = &t->p_listnode;

	DEBUGASSERT(addee->pln_prev == NULL);
	DEBUGASSERT(addee->pln_next == NULL);

	addee->pln_prev = onlist;
	addee->pln_next = onlist->pln_next;
	addee->pln_prev->pln_next = addee;
	addee->pln_next->pln_prev = addee;
}

/*
 * Do insertion. Doesn't update pl_count.
 */
static
void
processlist_insertbeforenode(struct process *t, struct processlistnode *onlist)
{
	struct processlistnode *addee;

	addee = &t->p_listnode;

	DEBUGASSERT(addee->pln_prev == NULL);
	DEBUGASSERT(addee->pln_next == NULL);

	addee->pln_prev = onlist->pln_prev;
	addee->pln_next = onlist;
	addee->pln_prev->pln_next = addee;
	addee->pln_next->pln_prev = addee;
}

/*
 * Do removal. Doesn't update pl_count.
 */
static
void
processlist_removenode(struct processlistnode *pln)
{
	DEBUGASSERT(pln != NULL);
	DEBUGASSERT(pln->pln_prev != NULL);
	DEBUGASSERT(pln->pln_next != NULL);

	pln->pln_prev->pln_next = pln->pln_next;
	pln->pln_next->pln_prev = pln->pln_prev;
	pln->pln_prev = NULL;
	pln->pln_next = NULL;
}

////////////////////////////////////////////////////////////
// public

void
processlist_addhead(struct processlist *pl, struct process *t)
{
	DEBUGASSERT(pl != NULL);
	DEBUGASSERT(t != NULL);

	processlist_insertafternode(&pl->pl_head, t);
	pl->pl_count++;
}

void
processlist_addtail(struct processlist *pl, struct process *t)
{
	DEBUGASSERT(pl != NULL);
	DEBUGASSERT(t != NULL);

	processlist_insertbeforenode(t, &pl->pl_tail);
	pl->pl_count++;
}

struct process *
processlist_remhead(struct processlist *pl)
{
	struct processlistnode *pln;

	DEBUGASSERT(pl != NULL);

	pln = pl->pl_head.pln_next;
	if (pln->pln_next == NULL) {
		/* list was empty  */
		return NULL;
	}
	processlist_removenode(pln);
	DEBUGASSERT(pl->pl_count > 0);
	pl->pl_count--;
	return pln->pln_self;
}

struct process *
processlist_remtail(struct processlist *pl)
{
	struct processlistnode *pln;

	DEBUGASSERT(pl != NULL);

	pln = pl->pl_tail.pln_prev;
	if (pln->pln_prev == NULL) {
		/* list was empty  */
		return NULL;
	}
	processlist_removenode(pln);
	DEBUGASSERT(pl->pl_count > 0);
	pl->pl_count--;
	return pln->pln_self;
}

void
processlist_insertafter(struct processlist *pl,
		       struct process *onlist, struct process *addee)
{
	processlist_insertafternode(&onlist->p_listnode, addee);
	pl->pl_count++;
}

void
processlist_insertbefore(struct processlist *pl,
			struct process *addee, struct process *onlist)
{
	processlist_insertbeforenode(addee, &onlist->p_listnode);
	pl->pl_count++;
}

void
processlist_remove(struct processlist *pl, struct process *t)
{
	processlist_removenode(&t->p_listnode);
	DEBUGASSERT(pl->pl_count > 0);
	pl->pl_count--;
}

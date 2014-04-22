/* Swap Space Header */

#ifndef _SWAPSPACE_H_
#define _SWAPSPACE_H_

#include <types.h>
#include <thread.h>
#include <current.h>
#include <vnode.h>

/* Initialize the hdd 1 for page swapping. */
int swapspace_init(void);

#endif /* _SWAPSPACE_H_ */


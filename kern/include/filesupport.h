/* File Handle and File Object Header*/

#ifndef _FILESUPPORT_H_
#define _FILESUPPORT_H_

#include <types.h>
#include <thread.h>
#include <vnode.h>

/* File Handle Structure */
struct file_handle {
	char fh_name[PATH_MAX];
	// char *fh_name;
	// Pointer to our file object
	struct vnode *vnode;

	// 64-bit offset into file.
	off_t fh_offset;

	// flags for R, RW, ...
	int fh_flags;

	// Count of number of open references to file handle.
	// If zero, file handle can be destroyed.
	int fh_open_count;

	// Lock to make inc/dec of open count atomic.
	struct lock *fh_open_lk;
};

struct file_handle *fh_create(char*, int flags);
void fh_destroy(struct file_handle*);

#endif /* _FILESUPPORT_H_ */

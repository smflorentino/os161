/* File Handle and File Object Header*/

#ifndef _FILESUPPORT_H_
#define _FILESUPPORT_H_

#include <types.h>
#include <thread.h>
#include <vnode.h>

/* File Object Structure */
struct file_object {
	char *fo_name;

	// Pointer to vnode, an abstract representation of a file.
	struct vnode *fo_vnode;

	// Lock to make I/O to the file atomic.
	struct lock *fo_vnode_lk;
};

struct file_object *fo_create(const char*);
void fo_destroy(const char*);

void fo_vnode_lock_acquire(void);
void fo_vnode_lock_release(void);
bool fo_vnode_lock_do_i_hold(void);


/* File Handle Structure */
struct file_handle {
	char *fh_name;

	// Reference to file handle object.
	struct file_object *fh_file_object;

	// 64-bit offset into file.
	uint64_t fh_offset;

	// flags for R, RW, ...
	int fh_flags;

	// Count of number of open references to file handle.
	// If zero, file handle can be destroyed.
	int fh_open_count;

	// Lock to make inc/dec of open count atomic.
	struct lock *fh_open_lk;
};

struct file_handle *fh_create(const char*, int flags);
void fh_destroy(const char*);

void fh_open_lock_acquire(void);
void fh_open_lock_release(void);
bool fh_open_lock_do_i_hold(void);

#endif /* _FILESUPPORT_H_ */

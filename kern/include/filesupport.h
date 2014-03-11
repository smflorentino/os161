/* File Handle and File Object Header*/

#ifndef _FILESUPPORT_H_
#define _FILESUPPORT_H_

#define FO_MAX	10	/* Change to FILE_MAX at some point. */

#include <types.h>
#include <thread.h>
#include <vnode.h>

// The file object list. Used to point to all file objects on the system.
extern struct file_object* file_object_list[FO_MAX];

// Global array of file object pointers.
// TODO: initialize all pointer to NULL at boot time.
//struct file_object*  file_object_list[OPEN_MAX*PID_MAX];
//int check_file_object_list(char*);

/* File Object Structure */
struct file_object {
	char fo_name[NAME_MAX];

	// Pointer to vnode, an abstract representation of a file.
	struct vnode *fo_vnode;

	// Lock to make I/O to the file atomic.
	struct lock *fo_vnode_lk;
};



struct file_object *fo_create(const char*);
void fo_destroy(struct file_object*);

void fo_vnode_lock_acquire(void);
void fo_vnode_lock_release(void);
bool fo_vnode_lock_do_i_hold(void);

// Check if the file handle exists yet, or not. Return index int if it does or -1 if it doesn't.
int check_file_object_list(char*, int*);
void file_object_list_init(void);

/* File Handle Structure */
struct file_handle {
	char fh_name[NAME_MAX];

	// Reference to file handle object.
	struct file_object *fh_file_object;

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

struct file_handle *fh_create(const char*, int flags);
void fh_destroy(struct file_handle*);

void fh_open_lock_acquire(void);
void fh_open_lock_release(void);
bool fh_open_lock_do_i_hold(void);

#endif /* _FILESUPPORT_H_ */

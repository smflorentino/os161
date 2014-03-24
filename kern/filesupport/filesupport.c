/*

	Handling of File Handles and File Objects
*/

	#include <types.h>
	#include <lib.h>
	#include <synch.h>
	#include <filesupport.h>
	#include <vnode.h>

	/*
	*	File Handle Functions
	*/

	struct file_handle *
	fh_create(char *name, int flags)
	{
		struct file_handle *fh;

		fh = kmalloc(sizeof(struct file_handle));
		if(fh == NULL) {
			return NULL;
		}
		// fh->fh_name = kstrdup(name);
		strcpy(fh->fh_name, name);

		fh->vnode = kmalloc(sizeof(struct vnode*));
		if(fh->vnode == NULL) {
			kfree(fh->fh_name);
			kfree(fh);
			return NULL;
		}

		fh->fh_open_lk = lock_create(name);
		if(fh->fh_open_lk == NULL) {
			kfree(fh->fh_name);
			kfree(fh->vnode);
			kfree(fh);
			return NULL;
		}

		// One process now has this file handle referenced.
		fh->fh_open_count = 1; // Should be 1
		fh->fh_flags = flags;

		// Start at the beginning of the file.
		fh->fh_offset = 0;

		return fh;
	}

	void
	fh_destroy(struct file_handle *fh)
	{
		KASSERT(fh->fh_open_count == 0);
		lock_destroy(fh->fh_open_lk);
		// kfree(fh->fh_name);
		// kfree(fh->vnode);
		kfree(fh);
	}



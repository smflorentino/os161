/*

	Handling of File Handles and File Objects
*/

	#include <types.h>
	#include <lib.h>
	#include <synch.h>
	#include <filesupport.h>
	#include <vnode.h>

	/*
	*	File Object Fuctions
	*
	*/

	/* Create a new file object. */
	struct file_object *
	fo_create(const char *name)
	{
		struct file_object *fo;

		fo = kmalloc(sizeof(struct file_object));
		if(fo == NULL) {
			return NULL;
		}

		fo->fo_name = kstrdup(name);
		if(fo->fo_name == NULL) {
			kfree(fo);
			return NULL;
		}

		fo->fo_vnode_lk = lock_create(name);
		if(fo->fo_vnode_lk == NULL) {
			kfree(fo->fo_name);
			kfree(fo);
			return NULL;
		}

		return fo;
	}

	void
	fo_destroy(const char *name)
	{
		// Method to identify particular fo to destroy.
		//struct file_object *fo = ;
		/*
		lock_destroy(fo->fo_vnode_lk);
		kfree(fo->fo_name);
		kfree(fo);
		*/

		(void) name;
	}

	void
	fo_vnode_lock_acquire(void)
	{
		(void) 1;
	}

	void
	fo_vnode_lock_release(void)
	{
		(void) 1;
	}

	bool
	fo_vnode_lock_do_i_hold(void)
	{
		//void) 1;
		return 1;
	}


	/*
	*	File Handle Functions
	*
	*/

	struct file_handle *
	fh_create(const char *name, int flags)
	{
		struct file_handle *fh;

		fh = kmalloc(sizeof(struct file_handle));
		if(fh == NULL) {
			return NULL;
		}

		// Is this correct for the fh_file_object pointer?
		fh->fh_file_object = kmalloc(sizeof(struct file_object*));
		if(fh->fh_file_object == NULL) {
			kfree(fh);
			return NULL;
		}

		fh->fh_open_lk = lock_create(name);
		if(fh->fh_open_lk == NULL) {
			kfree(fh->fh_name);
			kfree(fh);
			return NULL;
		}

		// One process now has this file handle referenced.
		fh->fh_open_count = 1;
		fh->fh_flags = flags;

		// Start at the beginning of the file.
		fh->fh_offset = 0;

		return fh;
	}

	void
	fh_destroy(const char *name)
	{
		// Method of getting file handle to be destroyed.
		//struct file_handle *fh = ;
		/*
		lock_destroy(fh->fh_open_lk);
		kfree(fh->fh_name);
		kfree(fh);
		*/
		(void) name;
	}

	void
	fh_open_lock_acquire(void)
	{
		(void) 1;
	}

	void
	fh_open_lock_release(void)
	{
		(void) 1;
	}

	bool
	fh_open_lock_do_i_hold(void)
	{
		return 1;
	}



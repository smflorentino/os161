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

	// Global file object list.
	struct file_object* file_object_list[FO_MAX];

	/* Create a new file object. */
	struct file_object *
	fo_create(const char *name)
	{
		struct file_object *fo;

		fo = kmalloc(sizeof(struct file_object));
		if(fo == NULL) {
			return NULL;
		}

		strcpy(fo->fo_name, name);
		/*kstrdup(name);
		if(fo->fo_name == NULL) {
			kfree(fo);
			return NULL;
		}*/

		fo->fo_vnode = kmalloc(sizeof(struct vnode));
		if(fo->fo_vnode == NULL) {
			//kfree(fo->fo_name);
			kfree(fo);
			return NULL;
		}

		fo->fo_vnode_lk = lock_create(name);
		if(fo->fo_vnode_lk == NULL) {
			kfree(fo->fo_vnode);
			//kfree(fo->fo_name);
			kfree(fo);
			return NULL;
		}

		// Add this new file object to the global file object list.
		fo_vnode_lock_acquire();
		for(int i = 0; i < FO_MAX; i++) {
			if(file_object_list[i] == NULL) {
				file_object_list[i] = fo;
				break;
			}
		}
		fo_vnode_lock_release();

		return fo;
	}

	void
	fo_destroy(struct file_object *fo)
	{
		// Method to identify particular fo to destroy.
		//struct file_object *fo = ;
		/*
		lock_destroy(fo->fo_vnode_lk);
		kfree(fo->fo_name);
		kfree(fo);
		*/

		(void) fo;
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

	// Searches the file object list array for a fo with a particular name.
	// Returns index value for fo if it exists; returns -1 if it does not.
	int
	check_file_object_list(char *filename, int *free_index)
	{
		//(void)filename;
		bool found_free = false;
		
		struct file_object *fo;
		fo_vnode_lock_acquire();
		for(int i = 0; i < FO_MAX; i++) {
			fo = file_object_list[i];
			// Find a free index, incase we need to add a file object.
			if((fo == NULL) && (!found_free)) {
				found_free = true;
				*free_index = i;
			}
			// See if our desired file object already exists.
			if((fo != NULL) && (strcmp(fo->fo_name,filename) == 0)) {
				// File object already exists, pass back its index.
				fo_vnode_lock_release();
				return i;
			}
		}
		fo_vnode_lock_release();
		
		// No file object by that name exists yet.
		return -1;
	}

	void
	file_object_list_init(void)
	{
		for(int i=0; i < FO_MAX; i++) {
			file_object_list[i] = NULL;
		}
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

		strcpy(fh->fh_name, name);
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
	fh_destroy(struct file_handle *fh)
	{
		lock_destroy(fh->fh_open_lk);
		kfree(fh->fh_file_object);
		kfree(fh->fh_name);
		kfree(fh);
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



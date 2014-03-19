/*

	Handling of File Handles and File Objects
*/

	#include <types.h>
	#include <lib.h>
	#include <synch.h>
	#include <filesupport.h>
	#include <vnode.h>

	
	// Global file object list.
	struct file_object* file_object_list[FO_MAX];
	const char fo_list_lk_name[] = "fo_list_lock";
	struct lock* fo_list_lock;

	/*
	*	File Object Fuctions
	*/

	/* Create a new file object. */
	struct file_object *
	fo_create(char *name)
	{
		struct file_object *fo;

		fo = kmalloc(sizeof(struct file_object));
		if(fo == NULL) {
			return NULL;
		}

		strcpy(fo->fo_name, name);

		fo->fo_vnode = kmalloc(sizeof(struct vnode));
		if(fo->fo_vnode == NULL) {
			kfree(fo);
			return NULL;
		}

		fo->fo_vnode_lk = lock_create(name);
		if(fo->fo_vnode_lk == NULL) {
			kfree(fo->fo_vnode);
			kfree(fo);
			return NULL;
		}

		// Add this new file object to the global file object list.
		//lock_acquire(fo_list_lock);
		for(int i = 0; i < FO_MAX; i++) {
			// Find the first free index in the list; add the new fo to it.
			if(file_object_list[i] == NULL) {
				file_object_list[i] = fo;
				break;
			}
		}
		//lock_release(fo_list_lock);

		return fo;
	}

	void
	fo_destroy(struct file_object *fo)
	{
		// Remove the fo from the global fo list.
		lock_acquire(fo_list_lock);
		for(int i = 0; i < FO_MAX; i++) {
			// Find the first free index in the list.
			if(file_object_list[i] == fo) {
				file_object_list[i] = NULL;
				break;
			}
		}
		lock_release(fo_list_lock);

		// Remove the rest of the fo information.
		lock_destroy(fo->fo_vnode_lk);
		fo->fo_vnode = NULL;
		kfree(fo->fo_vnode);
		kfree(fo);
	}

	// Searches the file object list array for a fo with a particular name.
	// Returns index value for fo if it exists; returns -1 if it does not.
	int
	check_file_object_list(char *filename, int *free_index)
	{
		bool found_free = false;
		struct file_object *fo;

		
		for(int i = 0; i < FO_MAX; i++) {
			fo = file_object_list[i];
			// Find a free index, in case we need to add a file object.
			if((fo == NULL) && (!found_free)) {
				found_free = true;
				*free_index = i;
			}
			// See if our desired file object already exists.
			if((fo != NULL) && (strcmp(fo->fo_name,filename) == 0)) {
				// File object already exists, pass back its index.
				//lock_release(fo_list_lock);
				return i;
			}
		}
		
		
		// No file object by that name exists yet.
		return -1;
	}

	int
	file_object_list_init(void)
	{
		// NULL out the file object pointer list for the first and only time
		for(int i=0; i < FO_MAX; i++) {
			file_object_list[i] = NULL;
		}

		// Create the list lock, since the list is global
		fo_list_lock = lock_create(fo_list_lk_name);
		if(fo_list_lock == NULL) {
			return -1;
		}

		return 0;
	}

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

		strcpy(fh->fh_name, name);

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
		fh->fh_file_object = NULL;
		lock_destroy(fh->fh_open_lk);
		kfree(fh->fh_file_object);
		kfree(fh);
	}



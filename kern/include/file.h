#ifndef _FILE_H_
#define _FILE_H_

#include <types.h>
#include <vnode.h>
#include <synch.h>

#define MAX_FILES_PER_PROCESS 32

struct global_file_handler {
	struct vnode *vnode;
	off_t offset;
	int open_count;
	int open_flags;
	struct lock *flock;
	
	/* 
	 * this will eventually become a list
	 * containing all the file_handlers
	 * struct global_filetable_list *next;
	 * struct global_filetable_list *prev;
	 */
};
#endif

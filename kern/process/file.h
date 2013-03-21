#include <types.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>

#define MAX_FILES_PER_PROCESS

struct global_filetable_list {
	struct vnode;
	off_t offset;
	int open_count;
	int open_flags;
	struct lock flock;
	struct global_filetable_list *next;
	struct global_filetable_list *prev;
}

extern global_filetable_list* process_filetable[MAX_FILES_PER_PROCESS];

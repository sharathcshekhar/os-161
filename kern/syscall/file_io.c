/*
 * file_io.c
 *
 *  Created on: Apr 1, 2013
 *      Author: trinity
 */

#include <kern/errno.h>
#include <file.h>
#include <syscall.h>
#include <stat.h>
#include <thread.h>
#include <current.h>
#include <types.h>
#include <kern/syscall.h>
#include <lib.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <vnode.h>
#include <uio.h>
#include <vnode.h>
#include <vfs.h>

/***********************************************************************
 * OPEN (STILL DRAFT) //TODO: not anymore :)
 * List of errors to Return:
 * 1. If source = NULL, Return EFAULT
 * 2. If strncpy_user fails, return -1:
 *
 * After vfs_open if
 * 1. Result = 0 then assign File Descriptor
 * 2. Result = 25 then ENODEV error has occurred
 * 3. Result = 17 then ENOTDIR error has occurred
 * 4. Result = 19 then ENOENT error has occurred
 * 5. Result = 22 then EEXIST error has occurred
 * 6. Result = 18 then EISDIR error has occurred
 * 7. Result = 28 then EMFILE error has occurred
 * 8. Result = 29 then ENFILE error has occurred
 * 9. Result = 26 then ENXIO error has occurred
 * 10. Result = 36 then ENOSPC error has occurred
 * 11. Result = 8 then EINVAL error has occurred
 * 12. Result = 32 then EIO error has occurred
 **********************************************************************/

int
sys_open(userptr_t u_file, int flags, int mode, int *fd_ret){

	int result;
	int fd = 0, i;
	size_t actual;
	/* TODO: Memory leak! free it before returning */
	char *k_file = kmalloc(MAX_PATH);
	struct stat *ptr = kmalloc(sizeof(struct stat));

	struct vnode *vnode;

	if (curthread->process_table->open_file_count == MAX_FILES_PER_PROCESS) {
		return EMFILE;
	}

	if (((flags & O_ACCMODE) != O_RDONLY) && ((flags & O_ACCMODE) != O_WRONLY) &&
				((flags & O_ACCMODE) != O_RDWR)) {
		return EINVAL;
	}
	
	if (((flags & O_EXCL) == O_EXCL) && ((flags & O_CREAT) != O_CREAT)) {
			return EINVAL;
	}

	result = copyinstr(u_file, k_file, MAX_PATH, &actual);
	if (result) {
		return result;
	}

	result = vfs_open(k_file, flags, mode, &vnode);
	if (result) {
		return result;
	}
	
	for (i = 0; i < MAX_FILES_PER_PROCESS; i++) {
		if (curthread->process_table->file_table[i] == NULL) {
			fd = i;
			break;
		}
	}
	//TODO init fd to -1 and KASSERT fd != -1
	
	curthread->process_table->file_table[fd] = kmalloc(sizeof(struct global_file_handler));
	//TODO: KASSERT if malloc has gone through?
	curthread->process_table->file_table[fd]->vnode = vnode;
	curthread->process_table->file_table[fd]->open_count = 1;
	curthread->process_table->file_table[fd]->open_flags = flags;
	curthread->process_table->file_table[fd]->flock = lock_create("file_handler_lk");
	//TODO: Increment per process file count
	
	if (flags & O_APPEND) {
		result = VOP_STAT(curthread->process_table->file_table[fd]->vnode, ptr);
		if (result) {
			return result;
		}
		curthread->process_table->file_table[fd]->offset = ptr->st_size;
	} else {
		curthread->process_table->file_table[fd]->offset = 0;
	}
	
	*fd_ret = fd;
	return 0;
}

int
sys_write(int fd, userptr_t buf, int size, int *bytes_written)
{
	int ret, access_mode;
	struct uio k_uio;
	struct iovec k_iov;
	off_t offset;

	//TODO: check if (0 < fd  < MAX_FILES_PER_PROCESS), you might fault or else
	/* check if fd passed has a valid entry in the process file table */
	if (curthread->process_table->file_table[fd] == NULL) {
		return EBADF;
	}

	access_mode = (curthread->process_table->file_table[fd]->open_flags) &
					O_ACCMODE;
	if ((access_mode == O_WRONLY) || (access_mode ==  O_RDWR)) {
		offset = curthread->process_table->file_table[fd]->offset;
		uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_WRITE);
		ret = VOP_WRITE(curthread->process_table->file_table[fd]->vnode, &k_uio);
		if (ret) {
			return ret;
		}
		offset += size;
		/* acquire lock */
		lock_acquire(curthread->process_table->file_table[fd]->flock);
		/* should this be incremented by size of set to offset? race condition with child/parent? */
		curthread->process_table->file_table[fd]->offset = offset;
		lock_release(curthread->process_table->file_table[fd]->flock);
		/* 
		 * right way to get the bytes written? why would you not write
		 * all the bytes requested anyway? 
		 */
		*bytes_written = size - k_uio.uio_resid;
		return 0;
	} else {
		/* return ENOPERMS ?*/
		return -1;
	}
}

int
sys_read(int fd, void *buf, int size, int *bytes_read)
{
	int access_mode, result;
	off_t offset;
	struct uio k_uio;
	struct iovec k_iov;

	if (curthread->process_table->file_table[fd] == NULL) {
			return EBADF;
	}
	//TODO: Check if the fd is valid, you will fault or else!
	access_mode = (curthread->process_table->file_table[fd]->open_flags) & O_ACCMODE;
	if ((access_mode == O_RDONLY) || (access_mode == O_RDWR)) {

		offset = curthread->process_table->file_table[fd]->offset;
		uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_READ);
		result = VOP_READ(curthread->process_table->file_table[fd]->vnode, &k_uio);
		if (result) {
			return result;
		}
		offset += size;

		lock_acquire(curthread->process_table->file_table[fd]->flock);
		/* should this be incremented by size of set to offset? race condition with child/parent? */
		curthread->process_table->file_table[fd]->offset = offset;
		lock_release(curthread->process_table->file_table[fd]->flock);

		*bytes_read = size-k_uio.uio_resid;
		return 0;
	} 
	return -1;
}

int
sys_close(int fd)
{
	if(curthread->process_table->file_table[fd] == NULL){
				return EBADF;
	}
	
	//TODO check if fd is valid? trying to close a unopened file?
	KASSERT(curthread->process_table->file_table[fd]->open_count > 0);

	/* pick a lock before decrementing reference counter */
	lock_acquire(curthread->process_table->file_table[fd]->flock);
	curthread->process_table->file_table[fd]->open_count--;
	lock_release(curthread->process_table->file_table[fd]->flock);
	
	//TODO: decrement per-process file counter
	if (curthread->process_table->file_table[fd]->open_count == 0) {
		vfs_close(curthread->process_table->file_table[fd]->vnode);
		lock_destroy(curthread->process_table->file_table[fd]->flock);
		kfree(curthread->process_table->file_table[fd]);
		curthread->process_table->file_table[fd] = NULL;
		//TODO: decrement global file count
	}
	return 0;
}

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
 * OPEN
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

	int result, i;
	int fd = -1;
	int offset = 0;
	size_t actual;
	char *k_file = kmalloc(MAX_PATH);
	struct vnode *vnode;
	struct global_file_handler *file_handler = NULL;

	if (curthread->process_table->open_file_count == MAX_FILES_PER_PROCESS) {
		result = EMFILE;
		goto end;
	}
	//TODO: if global_file_count > MAX_FILES, return error
	
	if (((flags & O_ACCMODE) != O_RDONLY) && ((flags & O_ACCMODE) != O_WRONLY) &&
				((flags & O_ACCMODE) != O_RDWR)) {
		result = EINVAL;
		goto end;
	}
	
	if (((flags & O_EXCL) == O_EXCL) && ((flags & O_CREAT) != O_CREAT)) {
		result = EINVAL;
		goto end;
	}

	result = copyinstr(u_file, k_file, MAX_PATH, &actual);
	if (result) {
		goto end;
	}

	result = vfs_open(k_file, flags, mode, &vnode);
	if (result) {
		goto end;
	}
	
	if (flags & O_APPEND) {
		struct stat file_stat;
		result = VOP_STAT(vnode, &file_stat);
		if (result) {
			vfs_close(vnode);
			goto end;
		}
		offset = file_stat.st_size;
	}
	/* Find the smallest fd in the table */	
	for (i = 0; i < MAX_FILES_PER_PROCESS; i++) {
		if (curthread->process_table->file_table[i] == NULL) {
			fd = i;
			break;
		}
	}
	
	KASSERT(fd > 0);
	curthread->process_table->open_file_count++;
	lock_acquire(global_file_count_lk);
	global_file_count++;
	lock_release(global_file_count_lk);
	
	file_handler = kmalloc(sizeof(struct global_file_handler));
	KASSERT(file_handler);

	file_handler->vnode = vnode;
	file_handler->open_count = 1;
	file_handler->open_flags = flags;
	file_handler->flock = lock_create("file_handler_lk");
	KASSERT(file_handler->flock);
	file_handler->offset = offset;
	
	curthread->process_table->file_table[fd] = file_handler;
	
	*fd_ret = fd;
	result = 0;

end:
	kfree(k_file);
	return result;
}

int
sys_write(int fd, userptr_t buf, int size, int *bytes_written)
{
	int ret, access_mode;
	struct uio k_uio;
	struct iovec k_iov;
	off_t offset;
	struct global_file_handler *file_handler = NULL;

	/* check if fd passed has a valid entry in the process file table */
	if ((fd < 0) || (fd > MAX_FILES_PER_PROCESS)) {
		return EBADF;
	}

	file_handler = curthread->process_table->file_table[fd];

	if (file_handler == NULL) {
		return EBADF;
	}

	access_mode = (file_handler->open_flags) & O_ACCMODE;
	if ((access_mode == O_WRONLY) || (access_mode ==  O_RDWR)) {
		offset = file_handler->offset;
		uio_uinit(&k_iov, &k_uio, buf, size, offset, UIO_WRITE);
		ret = VOP_WRITE(file_handler->vnode, &k_uio);
		if (ret) {
			return ret;
		}
		offset += size;
		/* acquire lock */
		lock_acquire(file_handler->flock);
		/* TOCHECK: should this be incremented by size of set to offset? race condition with child/parent? */
		file_handler->offset = offset;
		lock_release(file_handler->flock);
		/* 
		 * right way to get the bytes written? why would you not write
		 * all the bytes requested anyway? 
		 */
		*bytes_written = size - k_uio.uio_resid;
		return 0;
	} 
	return EBADF;
}

int
sys_read(int fd, void *buf, int size, int *bytes_read)
{
	int access_mode, result;
	off_t offset;
	struct uio k_uio;
	struct iovec k_iov;
	struct global_file_handler *file_handler = NULL;

	if ((fd < 0) || (fd > MAX_FILES_PER_PROCESS)) {
		return EBADF;
	}

	file_handler = curthread->process_table->file_table[fd];

	if (file_handler == NULL) {
		return EBADF;
	}

	access_mode = (file_handler->open_flags) & O_ACCMODE;
	if ((access_mode == O_RDONLY) || (access_mode == O_RDWR)) {

		offset = file_handler->offset;
		uio_uinit(&k_iov, &k_uio, buf, size, offset, UIO_READ);
		result = VOP_READ(file_handler->vnode, &k_uio);
		if (result) {
			return result;
		}
		offset += size;

		lock_acquire(file_handler->flock);
		/* TOCHECK: should this be incremented by size of set to offset? race condition with child/parent? */
		file_handler->offset = offset;
		lock_release(file_handler->flock);

		*bytes_read = size-k_uio.uio_resid;
		return 0;
	} 
	return -1;
}

int
sys_close(int fd)
{
	if ((fd < 0) || (fd > MAX_FILES_PER_PROCESS)) {
		return EBADF;
	} else if (curthread->process_table->file_table[fd] == NULL){
		return EBADF;
	}
	__close(fd);	
	return 0;
}

void __close(int fd)
{
	KASSERT((fd >= 0) && (fd <= MAX_FILES_PER_PROCESS));
	struct global_file_handler *file_handler = curthread->process_table->file_table[fd];
	KASSERT(file_handler->open_count > 0);
	
	lock_acquire(file_handler->flock);
	file_handler->open_count--;
	lock_release(file_handler->flock);

	curthread->process_table->open_file_count--;
	curthread->process_table->file_table[fd] = NULL;

	if (file_handler->open_count == 0) {
		vfs_close(file_handler->vnode);
		lock_destroy(file_handler->flock);
		kfree(file_handler);
		
		lock_acquire(global_file_count_lk);
		global_file_count--;
		lock_release(global_file_count_lk);
	}
}

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

	int result;
	int fd = -1, i;
	size_t actual;
	char *k_file = kmalloc(MAX_PATH);
	struct stat file_stat;
	struct vnode *vnode;


	if (curthread->process_table->open_file_count == MAX_FILES_PER_PROCESS) {
		result = EMFILE;
		goto end;
	}

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

	lock_acquire(global_file_count_lk);
	global_file_count++;
	lock_release(global_file_count_lk);
	
	for (i = 0; i < MAX_FILES_PER_PROCESS; i++) {
		if (curthread->process_table->file_table[i] == NULL) {
			fd = i;
			break;
		}
	}
	
	KASSERT(fd >0);

	struct global_file_handler *file_Handler = curthread->process_table->file_table[fd];

	file_Handler = kmalloc(sizeof(struct global_file_handler));

	KASSERT(file_Handler);

	file_Handler->vnode = vnode;
	file_Handler->open_count = 1;
	file_Handler->open_flags = flags;
	file_Handler->flock = lock_create("file_handler_lk");
	curthread->process_table->open_file_count++;

	if (flags & O_APPEND) {
		result = VOP_STAT(file_Handler->vnode, &file_stat);
		if (result) {
			goto end;
		}
		file_Handler->offset = file_stat.st_size;
	} else {
		file_Handler->offset = 0;
	}
	
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
	struct global_file_handler *file_Handler = curthread->process_table->file_table[fd];


	/* check if fd passed has a valid entry in the process file table */

	if(!(0 < fd) && (fd < MAX_FILES_PER_PROCESS)){
		return EBADF;
	}

	if (file_Handler == NULL) {
		return EBADF;
	}

	access_mode = (file_Handler->open_flags) &
					O_ACCMODE;
	if ((access_mode == O_WRONLY) || (access_mode ==  O_RDWR)) {
		offset = file_Handler->offset;
		uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_WRITE);
		ret = VOP_WRITE(file_Handler->vnode, &k_uio);
		if (ret) {
			return ret;
		}
		offset += size;
		/* acquire lock */
		lock_acquire(file_Handler->flock);
		/* should this be incremented by size of set to offset? race condition with child/parent? */
		file_Handler->offset = offset;
		lock_release(file_Handler->flock);
		/* 
		 * right way to get the bytes written? why would you not write
		 * all the bytes requested anyway? 
		 */
		*bytes_written = size - k_uio.uio_resid;
		return 0;
	} else {
		/* return ENOPERMS ?*/
		return EBADF;
	}
}

int
sys_read(int fd, void *buf, int size, int *bytes_read)
{
	int access_mode, result;
	off_t offset;
	struct uio k_uio;
	struct iovec k_iov;
	struct global_file_handler *file_Handler = curthread->process_table->file_table[fd];

	if(!((0 < fd) && (fd < MAX_FILES_PER_PROCESS) )){
		return EBADF;
	}

	if (file_Handler == NULL) {
		return EBADF;
	}

	access_mode = (file_Handler->open_flags) & O_ACCMODE;
	if ((access_mode == O_RDONLY) || (access_mode == O_RDWR)) {

		offset = file_Handler->offset;
		uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_READ);
		result = VOP_READ(file_Handler->vnode, &k_uio);
		if (result) {
			return result;
		}
		offset += size;

		lock_acquire(file_Handler->flock);
		/* should this be incremented by size of set to offset? race condition with child/parent? */
		file_Handler->offset = offset;
		lock_release(file_Handler->flock);

		*bytes_read = size-k_uio.uio_resid;
		return 0;
	} else {
		return -1;
	}
}

int
sys_close(int fd)
{
	struct global_file_handler *file_Handler = curthread->process_table->file_table[fd];

	if(!((0 < fd) && (fd < MAX_FILES_PER_PROCESS))){
			return EBADF;
	}

	if(file_Handler == NULL){
				return EBADF;
	}
	
	KASSERT(file_Handler->open_count > 0);

	/* pick a lock before decrementing reference counter */
	lock_acquire(file_Handler->flock);
	file_Handler->open_count--;
	lock_release(file_Handler->flock);
	

	curthread->process_table->open_file_count--;
	if (file_Handler->open_count == 0) {
		vfs_close(file_Handler->vnode);
		lock_destroy(file_Handler->flock);
		kfree(file_Handler);
		file_Handler = NULL;


		lock_acquire(global_file_count_lk);
		global_file_count--;
		lock_release(global_file_count_lk);
	}
	return 0;
}

int
__close(int fd){

	struct global_file_handler *file_Handler = curthread->process_table->file_table[fd];

	lock_acquire(file_Handler->flock);
	file_Handler->open_count--;
	lock_release(file_Handler->flock);

	curthread->process_table->open_file_count--;

	if (file_Handler->open_count == 0) {
			vfs_close(file_Handler->vnode);
			lock_destroy(file_Handler->flock);
			kfree(file_Handler);
			file_Handler = NULL;


			lock_acquire(global_file_count_lk);
			global_file_count--;
			lock_release(global_file_count_lk);
	}
	return 0;
}

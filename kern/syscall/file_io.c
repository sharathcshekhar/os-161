/*
 * file_io.c
 *
 *  Created on: Apr 1, 2013
 *      Author: trinity
 */

//#include <null.h>
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
 * OPEN (STILL DRAFT)
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
sys_open(userptr_t u_file, int flags, int mode, int *fileDescriptor){

	int result;
	off_t offset;
	int fd,i;
	char *k_file = kmalloc(MAX_PATH);
	struct stat *ptr = kmalloc(sizeof(struct stat));
	size_t *actual = kmalloc(sizeof(*actual));

	struct vnode *tempNode;


	if(curthread->process_table->open_file_count > MAX_FILES_PER_PROCESS){
		return EMFILE;
	}
	// have to compare with global

	/*if(curthread->process_table->file_table[fd]->open_count > NO_OF_GLOBAL_FILES){
		return ENFILE; //This needs to be checked
	}*/

	if(u_file == NULL){
		return EFAULT;
	}

	/*if(!(flags & O_RDONLY) && !(flags & O_WRONLY) && !(flags & O_RDWR)){
		return EINVAL;
	}
	else{
		if(!(flags &  O_CREAT) && !(flags & O_CREAT & O_EXCL) &&
				!(flags & O_TRUNC) && !(flags & O_APPEND)){
			return EINVAL;
		}
	}*/

	if( ((flags & O_ACCMODE) != O_RDONLY) && (((flags & O_ACCMODE) != O_WRONLY)) && ((flags & O_ACCMODE) != O_RDWR)){
		return EINVAL;
	}
	else{
		if(((flags & O_EXCL)==O_EXCL) && ((flags & O_CREAT) != O_CREAT)){
			return EINVAL;
		}
	}

	result = copyinstr(u_file, k_file, MAX_PATH, actual);

	if(result){
		return result;
	}

	result = vfs_open(k_file, flags, mode, &tempNode);


	if(result){
		return result;
	}
	else{
		//assign file descriptor here
		for(i=0;i<MAX_FILES_PER_PROCESS;i++){
			if(curthread->process_table->file_table[i] == NULL){
				break;
			}
		}
		fd=i;
		*fileDescriptor = fd;
		curthread->process_table->file_table[fd] = kmalloc(sizeof(struct global_file_handler));

		curthread->process_table->file_table[fd]->flock = lock_create("file_system_lk");

		curthread->process_table->file_table[fd]->vnode = tempNode;

		curthread->process_table->file_table[fd]->open_count = 1;
		curthread->process_table->file_table[fd]->open_flags = flags;

		if((flags & O_APPEND)==O_APPEND){
			result = VOP_STAT(curthread->process_table->file_table[fd]->vnode, ptr);
			if(result){
				return result;
			}
			offset = ptr->st_size;
			curthread->process_table->file_table[fd]->offset = offset;
		}
		else{
			curthread->process_table->file_table[fd]->offset = 0;
		}

	}
	return 0;
}

int
sys_write(int fd, userptr_t buf, int size, int *bytes_write)
{
	//char *str;
	int ret, flag;
	struct uio k_uio;
	struct iovec k_iov;
	off_t offset;


	/* supress warning */
	//check if fd passed has a valid entry in the process file table
	if(curthread->process_table->file_table[fd] == NULL){
		return EBADF;
	}

	flag = curthread->process_table->file_table[fd]->open_flags;
	if(((flag & O_ACCMODE)==O_WRONLY) || ((flag & O_ACCMODE)==O_RDWR))
	{
		if(fd){
			offset = curthread->process_table->file_table[fd]->offset;
			uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_WRITE);
			ret = VOP_WRITE(curthread->process_table->file_table[fd]->vnode, &k_uio);
			if(ret){
				return ret;
			}
			offset += size;
			// acquire lock
			lock_acquire(curthread->process_table->file_table[fd]->flock);
			curthread->process_table->file_table[fd]->offset = offset;
			lock_release(curthread->process_table->file_table[fd]->flock);
			*bytes_write = size-k_uio.uio_resid;
			return 0;//need to confirm this
		}
		else{
			return -1;
		}
		/*else{
			struct iovec iov;
			struct uio ku;
			uio_kinit(&iov, &ku, str, size, 0, UIO_WRITE);
			ret = VOP_WRITE(curthread->process_table->file_table[1]->vnode, &ku);
			return size;
		}*/
	}
	else{
		return -1;
	}
	//return 0;
}

int
sys_close(int fd){
	//int result;
	if(curthread->process_table->file_table[fd] == NULL){
				return EBADF;
	}



	KASSERT(curthread->process_table->file_table[fd]->open_count >= 0);

	//pick a lock before decrementing reference counter..
	lock_acquire(curthread->process_table->file_table[fd]->flock);
	curthread->process_table->file_table[fd]->open_count--;
	lock_release(curthread->process_table->file_table[fd]->flock);

	if(curthread->process_table->file_table[fd]->open_count == 0){
		vfs_close(curthread->process_table->file_table[fd]->vnode);
		lock_destroy(curthread->process_table->file_table[fd]->flock);
		kfree(curthread->process_table->file_table[fd]);
		curthread->process_table->file_table[fd] = NULL;
	}

	/*if(result){
		return result;
	}*/

	//decrement global file count
	//destroy lock

	//kfree(curthread->process_table->file_table[fd]->flock);

	//destroy file table entry of fd



	//curthread->process_table->file_table[fd]->vnode = NULL;
	//curthread->process_table->file_table[fd]->offset=0;
	return 0;
}

int
sys_read(int fd, void *buf, int size, int *bytes_read){

	int flag, result;
	off_t offset;
	struct uio k_uio;
	struct iovec k_iov;

	if(curthread->process_table->file_table[fd] == NULL){
			return EBADF;
	}
	flag = curthread->process_table->file_table[fd]->open_flags;
	if(((flag & O_ACCMODE) == O_RDONLY) || ((flag & O_ACCMODE) == O_RDWR)){

		offset = curthread->process_table->file_table[fd]->offset;
		uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_READ);
		result = VOP_READ(curthread->process_table->file_table[fd]->vnode, &k_uio);
		if(result){
			return result;
		}
		offset += size;

		// acquire lock
		lock_acquire(curthread->process_table->file_table[fd]->flock);
		curthread->process_table->file_table[fd]->offset = offset;
		lock_release(curthread->process_table->file_table[fd]->flock);

		*bytes_read = size-k_uio.uio_resid;
		return 0;//need to confirm this
	}
	else{
		return -1;
	}
	//return 0;
}


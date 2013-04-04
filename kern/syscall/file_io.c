#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <stat.h>

#include <copyinout.h>
#include <vnode.h>

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
sys_open(userptr_t u_file, int flags, int mode, int *retval){

	int result;
	int fd, i;
	size_t actual;
	char *k_file = kmalloc(MAX_PATH);
	struct stat *ptr = kmalloc(sizeof(struct stat));

	struct vnode *vnode;

	if (curthread->process_table->open_file_count == FILES_PER_PROCESS) {
		return EMFILE;
	}

	if (!(flags & O_RDONLY) && !(flags & O_WRONLY) && !(flags & O_RDWR)) {
		return EINVAL;
	}
	
	if (!(flags & O_CREAT) && !(flags & O_CREAT & O_EXCL) &&
				!(flags & O_TRUNC) && !(flags & O_APPEND)) {
		return EINVAL;
	}

	result = copyinstr(u_file, k_file, MAX_PATH, &actual);
	if (result) {
		return result;
	}

	result = vfs_open(k_file, flags, mode, &vnode);
	if(result){
		return result;
	}
	
	for (i = 0; i < FILES_PER_PROCESS; i++) {
		if (curthread->process_table->file_table[i] == NULL) {
			fd = i;
			break;
		}
	}
	
	curthread->process_table->file_table[fd] = kmalloc(sizeof(struct global_file_handler));
	curthread->process_table->file_table[fd]->vnode = vnode;

	curthread->process_table->file_table[fd]->open_count++;
	curthread->process_table->file_table[fd]->open_flags = flags;
	curthread->process_table->file_table[fd]->flock = lock_create("file_handler_lk");

	if (flags & O_APPEND) {
		result = VOP_STAT(curthread->process_table->file_table[fd]->vnode, ptr);
		if (result) {
			return result;
		}
		curthread->process_table->file_table[fd]->offset = ptr->st_size;
	} else {
		curthread->process_table->file_table[fd]->offset = 0;
	}
	
	*retval = fd;
	return 0;
}

/*
 * This should be replaced with a full fledged write 
 * and placed in a separate file
 */
int 
sys_write(int fd, userptr_t buf, int size, int *retval)
{
	char *str;
	int ret, flag;
	struct uio k_uio;
	struct iovec k_iov;
	off_t offset;

	/* supress warning */
	if(curthread->process_table->file_table[fd] == NULL){
		return EBADF;
	}

	flag = curthread->process_table->file_table[fd]->open_flags;
	if ((flag & O_WRONLY) || (flag & O_RDWR)) {
		offset = curthread->process_table->file_table[fd]->offset;
		uio_kinit(&k_iov, &k_uio, buf, size, offset, UIO_WRITE);
		ret = VOP_WRITE(curthread->process_table->file_table[fd]->vnode, &k_uio);
		if(ret){
			return ret;
		}
		offset += size;
		/* acquire lock */
		lock_acquire(curthread->process_table->file_table[fd]->flock);
		curthread->process_table->file_table[fd]->offset = offset;
		lock_release(curthread->process_table->file_table[fd]->flock);
		*retval = size-k_uio.uio_resid;//need to confirm this
		return 0;
	} else {
		return -1;
	}
}

int
sys_close(int fd){
	int result;

	curthread->process_table->file_table[fd]->open_count--;
	if(curthread->process_table->file_table[fd]->open_count == 0){
		vfs_close(curthread->process_table->file_table[fd]->vnode);
	}
}

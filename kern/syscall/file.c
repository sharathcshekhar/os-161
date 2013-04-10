#include <kern/seek.h>
#include <kern/errno.h>
#include <file.h>
#include <syscall.h>
#include <stat.h>
#include <thread.h>
#include <current.h>
#include <types.h>
#include <lib.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>

int
sys_lseek(int fd, off_t pos, userptr_t whence_ptr, off_t *new_pos)
{
	struct stat file_stat;
	int ret;
	int k_whence;
	
	ret = copyin(whence_ptr, (void*)&k_whence, sizeof(int));
	if (ret != 0) {
		return ret;
	}
	if (fd < 0 || fd > MAX_FILES_PER_PROCESS) {
		return EBADF;
	}
	if (curthread->process_table->file_table[fd] == NULL) {
		return EBADF;
	}
	
	/*
	 * You can seek beyond EOF
	 * Not sure if VFS will take care of this
	 */
	switch (k_whence) {
		case SEEK_SET:
			*new_pos = pos;
			break;
		case SEEK_CUR:
			*new_pos = curthread->process_table->file_table[fd]->offset + pos;
			break;
		case SEEK_END:
			ret = VOP_STAT(curthread->process_table->file_table[fd]->vnode, &file_stat);
			KASSERT(ret == 0);
			*new_pos = file_stat.st_size + pos;
			break;
		default:
			/* whence invalid */
			return EINVAL;
	}	
	
	ret = VOP_TRYSEEK(curthread->process_table->file_table[fd]->vnode, *new_pos);	
	if (ret != 0) {
		/* EINVAL for negative pos, ESPIPE for lseek on device */
		return ret;
	}
	
	lock_acquire(curthread->process_table->file_table[fd]->flock);
	curthread->process_table->file_table[fd]->offset = *new_pos;
	lock_release(curthread->process_table->file_table[fd]->flock);
	
	return 0;
}

int
sys_chdir(userptr_t pathname)
{
	int ret = 0;
	char *k_path = kmalloc(MAX_PATH);
	size_t len;
	KASSERT(k_path);
	
	ret = copyinstr(pathname, k_path, MAX_PATH, &len);
	if (ret != 0) {
		/* EFAULT, ENAMETOOLONG */
		kfree(k_path);
		return ret;
	}
	/* can return ENONET, ENOTDIR, EIO, ENODEV */
	ret = vfs_chdir(k_path);

	kfree(k_path);
	return ret;
}

int
sys___getcwd(userptr_t usr_buf, size_t buflen, int32_t *actual_len)
{
	int ret;
	struct uio k_uio;
	struct iovec k_iov;
	*actual_len = 0;

	uio_uinit(&k_iov, &k_uio, usr_buf, buflen, 0, UIO_READ);
	ret = vfs_getcwd(&k_uio);
	if (ret != 0) {
		/* EIO, EFAULT, ENOENT */
		return ret;
	}
	*actual_len = buflen - k_uio.uio_resid;
	return 0;
}

int
sys_dup2(int oldfd, int newfd, int *fd_ret)
{
	if (oldfd < 0 || oldfd > MAX_FILES_PER_PROCESS) {
		return EBADF;
	}
	
	if (curthread->process_table->file_table[oldfd] == NULL) {
		return EBADF;
	}
	
	if (newfd < 0 || newfd > MAX_FILES_PER_PROCESS) {
		return EBADF;
	}
	if (newfd == oldfd) {
		/* Don't do anything */
		*fd_ret = newfd;
		return 0;
	}
	//TODO: if newfd is already a dup of oldfd, shortcut and don't do anything
	
	if (curthread->process_table->file_table[newfd] != NULL) {
		__close(newfd);
	}
	
	if (curthread->process_table->open_file_count == MAX_FILES_PER_PROCESS) {
		/* TOCHECK: Why? why? redundant, but I didn't design this */
		return EMFILE;
	}
	
	curthread->process_table->open_file_count++;
	
	curthread->process_table->file_table[newfd] =
			curthread->process_table->file_table[oldfd];
	
	lock_acquire(curthread->process_table->file_table[oldfd]->flock);
	curthread->process_table->file_table[oldfd]->open_count++;
	lock_release(curthread->process_table->file_table[oldfd]->flock);
	
	/* redundant, but I didn't design this */
	*fd_ret = newfd;
	return 0;
}

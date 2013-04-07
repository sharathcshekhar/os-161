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
#include <vnode.h>
#include <vfs.h>
#include <uio.h>

int
sys_lseek(int fd, off_t pos, userptr_t whence_ptr, off_t *new_pos)
{
	struct stat *ptr = NULL;
	int result;
	//TODO: replace with copyin
	int whence = *((int *) (whence_ptr));
	
	/*
	 * You can seek beyond EOF
	 * Not sure if VFS will take care of this
	 */
	switch (whence) {
		case SEEK_SET:
			*new_pos = pos;
			break;
		case SEEK_CUR:
			*new_pos = curthread->process_table->file_table[fd]->offset + pos;
			break;
		case SEEK_END:
			ptr = kmalloc(sizeof(struct stat));
			result = VOP_STAT(curthread->process_table->file_table[fd]->vnode, ptr);
			KASSERT(result == 0);
			*new_pos = ptr->st_size + pos;
			kfree(ptr);
			break;
		default:
			return -1;
	}	
	
	if (*new_pos < 0) {
		return -1;
	}
	lock_acquire(curthread->process_table->file_table[fd]->flock);
	curthread->process_table->file_table[fd]->offset = *new_pos;
	lock_release(curthread->process_table->file_table[fd]->flock);
	
	return 0;
}

int
sys_chdir(userptr_t pathname)
{
	int result;
	char *k_path = kmalloc(MAX_PATH);
	size_t len;
	result = copyinstr(pathname, k_path, MAX_PATH, &len);
	vfs_chdir(k_path);
	kfree(k_path);
	return 0;
}

int
sys___getcwd(userptr_t buf, size_t buflen, int32_t *actual_len)
{
	int ret;
	struct uio k_uio;
	struct iovec k_iov;
	uio_kinit(&k_iov, &k_uio, buf, buflen, 0, UIO_READ);
	ret = vfs_getcwd(&k_uio);
	*actual_len = buflen - k_uio.uio_resid;
	return 0;
}

int
sys_dup2(int oldfd, int newfd, int *fd_ret)
{
	if (curthread->process_table->file_table[newfd] != NULL) {
		/* close this guy, need an implementation __close() */
	}
	curthread->process_table->file_table[newfd] =
			curthread->process_table->file_table[oldfd];
	curthread->process_table->file_table[oldfd]->open_count++;
	*fd_ret = newfd;
	return 0;
}

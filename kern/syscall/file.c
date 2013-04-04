#include <kern/seek.h>
#include <syscall.h>

int
sys_lseek(int fd, off_t pos, int whence, off_t *new_pos)
{
	struct stat *ptr = NULL;
	int result;
	/*
	 * You can seek beyond EOF
	 * Not sure if VFS will take care of this
	 */
	switch (whence) {
		case SEEK_SET:
			*new_pos = pos;
			break;
		case SEEK_CUR:
			*new_pos = curthread->ps_table->file_table[fd]->offset + pos;
			break;
		case SEEK_END:
			ptr = kmalloc(sizeof(struct stat));
			result = VOP_STAT(curthread->process_table->file_table[fd]->vnode, ptr);
			KASSERT(result == 0);
			*new_pos = ptr->st_size + pos;
			kfree(ptr);
			break;
		default:
			return -1:
	}	
	
	if (*new_pos < 0) {
		return -1;
	}
	lock_acquire(curthread->ps_table_file_table[fd]->flock);
	curthread->ps_table->file_table[fd]->offset = *new_pos;
	lock_release(curthread->ps_table_file_table[fd]->flock);
	
	return 0;
}

int
sys_chdir(userptr_t pathname)
{
	int result;
	char *k_path = kmalloc(MAX_PATH);
	size_t len;
	result = copyinstr(pathname, k_path, MAX_PATH, len);
	vfs_chdir(k_path);
	kfree(k_path);
	return 0;
}

int
sys___getcwd(userptr_t buf, size_t *buflen)
{
	int ret;
	struct uio k_uio;
	struct iovec k_iov;
	uio_kinit(&k_iov, &k_uio, *buflen, MAX_PATH, 0, UIO_WRITE);
	ret = vfs_getcwd(struct uio *k_uio);
	*buflen = k_uio.uio_resid;
	return 0;
}

int
sys_dup2(int oldfd, int newfd)
{
	if (curthread->ps_table->file_table[newfd] != NULL) {
		/* close this guy, need an implementation __close() */
	}
	curthread->ps_table->file_table[newfd] =
			curthread->ps_table->file_table[oldfd];
	curthread->ps_table->file_table[oldfd]->open_count++;
	retunr 0;
}

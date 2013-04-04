/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

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
//#include <null.h>

#include <uio.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <vfs.h>

/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */

/*int sys_write(int fd, userptr_t buf, int size);

int sys_open(userptr_t fileName, int flags, int mode);

int sys_close(int fd);*/

void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err;

	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

	case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0,
				(userptr_t)tf->tf_a1);
		break;

	case SYS_write:
		err = sys_write(tf->tf_a0, (userptr_t)tf->tf_a1,
				tf->tf_a2, &retval);
		break;

	case SYS_open:
		err = sys_open((userptr_t)tf->tf_a0, tf->tf_a1, tf->tf_a2, &retval);
		break;

	case SYS_read:
		err = sys_read(tf->tf_a0, (userptr_t)tf->tf_a1, tf->tf_a2, &retval);
		break;

	case SYS_close:
		err = sys_close(tf->tf_a0);
		break;
	
	case SYS_fork:
		err = sys_fork(tf, &retval);
		break;

	case SYS_getpid:
		err = sys_getpid(&retval);
		break;

	case SYS_waitpid:
		retval = tf->tf_a0;
		err = sys_waitpid(&retval, (userptr_t)tf->tf_a1,
				tf->tf_a2);
		break;

	case SYS__exit:
		sys__exit(tf->tf_a0);
		break;

	default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}

	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */

	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked process.
 *
 * This function is provided as a reminder. You need to write
 * both it and the code that calls it.
 *
 * Thus, you can trash it and do things another way if you prefer.
 */
void
enter_forked_process(struct trapframe *tf)
{
	(void)tf;
}


/*
 * This should be replaced with a full fledged write 
 * and placed in a separate file
 */

/*int
sys_write(int fd, userptr_t buf, int size)
{
	char *str;
	int ret, flag;
	struct uio k_uio;
	struct iovec k_iov;
	off_t offset;

	 //supress warning
	//check if fd passed has a valid entry in the process file table
	if(curthread->process_table->file_table[fd] == NULL){
		return EBADF;
	}

	flag = curthread->process_table->file_table[fd]->open_flags;
	if((flag & O_WRONLY) || (flag & O_RDWR))
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
			return size-k_uio.uio_resid;//need to confirm this
		}
		else{
			struct iovec iov;
			struct uio ku;
			uio_kinit(&iov, &ku, str, size, 0, UIO_WRITE);
			ret = VOP_WRITE(curthread->process_table->file_table[1]->vnode, &ku);
			return size;
		}
	}
	else{
		return -1;
	}
	return size;
}

*********************************************************************
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
 ********************************************************************

int
sys_open(userptr_t u_file, int flags, int mode){

	int result;
	off_t offset;
	int fd,i;
	char *k_file = kmalloc(MAX_PATH);
	struct stat *ptr = kmalloc(sizeof(struct stat));
	size_t *actual = kmalloc(sizeof(*actual));

	struct vnode *tempNode;

	if(curthread->process_table->open_file_count > FILES_PER_PROCESS){
		return EMFILE;
	}
	if(curthread->process_table->file_table[fd]->open_count > NO_OF_GLOBAL_FILES){
		return ENFILE; //This needs to be checked
	}

	if(u_file == NULL){
		return EFAULT;
	}

	if(!(flags & O_RDONLY) && !(flags & O_WRONLY) && !(flags & O_RDONLY)){
		return EINVAL;
	}
	else{
		if(!(flags &  O_CREAT) && !(flags & O_CREAT & O_EXCL) &&
				!(flags & O_TRUNC) && !(flags & O_APPEND)){
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
		for(i=0;i<FILES_PER_PROCESS;i++){
			if(curthread->process_table->file_table[i] == NULL){
				break;
			}
		}
		fd=i;

		curthread->process_table->file_table[fd]->vnode = tempNode;

		curthread->process_table->file_table[fd]->open_count++;
		curthread->process_table->file_table[fd]->open_flags = flags;

		if(flags & O_APPEND){
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
	return fd;
}

int
sys_close(int fd){
	int result;
	//pick a lock before decrementing reference counter..

	curthread->process_table->file_table[fd]->open_count--;
	if(curthread->process_table->file_table[fd]->open_count == 0){
		vfs_close(curthread->process_table->file_table[fd]->vnode);
	}

	if(result){
		return result;
	}
	curthread->process_table->file_table[fd]->vnode = NULL;
	curthread->process_table->file_table[fd]->offset=0;
	return 0;
}*/

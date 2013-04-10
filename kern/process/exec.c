#include <types.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <lib.h>
#include <addrspace.h>
#include <copyinout.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <mips/specialreg.h>

/* Max length of the prgram name */
#define MAX_PROGRAM_NAME 64

/* Max number of arguments supported */
#define MAX_ARGC 8

/* Max length of each argument */
#define MAX_ARGV_LEN 64

/* Bus Error. Take a train. */
#define ALIGN_PTR_TO_4BYTES(ptr)	((ptr) = ((ptr) - ((ptr) % 4)))

int sys_execv(userptr_t u_prog, userptr_t *u_argv, struct trapframe *tf)
{
	char *k_progname = kmalloc(MAX_PROGRAM_NAME);
	char *k_argv[MAX_ARGC + 1]; /* +1 for the NULL terminator */
	struct vnode *v;
   	struct addrspace *old_as = NULL;
	vaddr_t entrypoint, stackptr;
	uint32_t argv_offset;
	size_t len;
	int ret = 0, k_argc = 0, i = 0;

	ret = copyinstr(u_prog, k_progname, MAX_PROGRAM_NAME, &len);
	if (ret != 0) {
		/* ENAMETOOLONG */
		goto clean_exit;
	}
	//TODO: Allocate memory only once and not in a loop!	
	/* bad bad user, randcall passes a set of bad pointers! */
	do {
		userptr_t k_ptr;
		k_argv[k_argc] = kmalloc(MAX_ARGV_LEN);
		if (k_argv[k_argc] == NULL) {
			ret = ENOMEM;
			goto clean_exit;
		}
		ret = copyin((userptr_t)&u_argv[k_argc], (void*)&k_ptr, 4);
		if (ret != 0) {
			goto clean_exit;
		}
		if (k_ptr == NULL) {
			/* End of argument list */
			k_argv[k_argc] = NULL;
			break;
		}
		ret = copyinstr(k_ptr, k_argv[k_argc], MAX_ARGV_LEN, &len);
		if (ret != 0) {
			goto clean_exit;
		}
		k_argc++;
	} while (k_argc < MAX_ARGC); 
	
	if (k_argc == MAX_ARGC) {
		ret = E2BIG;
		goto clean_exit;
	}

	/* Open the exec file. */
	ret = vfs_open(k_progname, O_RDONLY, 0, &v);
	if (ret) {
		goto clean_exit;
	}
	
	/* restore in case of error, destory or else */
	old_as = curthread->t_addrspace;
	/* This is redundant in dumbvm */
	as_activate(NULL);
    
	curthread->t_addrspace = as_create();
	KASSERT(curthread->t_addrspace);
	/* Activate the new address space */
	as_activate(curthread->t_addrspace);
	
	ret = load_elf(v, &entrypoint);
	if (ret) {
		vfs_close(v);
		/* restore the old address space, destroy the new one */
		as_destroy(curthread->t_addrspace);
		curthread->t_addrspace = old_as;
		as_activate(curthread->t_addrspace);
		goto clean_exit;
	}

	/* Done with the file now. */
	vfs_close(v);
	ret = as_define_stack(curthread->t_addrspace, &stackptr);
	KASSERT(ret == 0);
	
	ret = copyout_args(k_argc, (void**)k_argv, &stackptr, &argv_offset);
	KASSERT(ret == 0);
	
	/* 
	 * if we have reached this point, everything has gone well and execv 
	 * has succeeded. zero the trapfram, and destory the old address space 
	 */
	bzero(tf, sizeof(struct trapframe));	
	as_destroy(old_as);
	
	tf->tf_a1 = argv_offset;
	tf->tf_sp = stackptr;
	tf->tf_epc = entrypoint;
	tf->tf_a0 = k_argc;
	/* I have no idea what the next line of code does! */
	tf->tf_status = CST_IRQMASK | CST_IEp | CST_KUp;
	ret = 0;

	//TODO: Add more labels and cleaner way to exit
clean_exit:
	/* free all the memory allocated for the arguments */
	kfree(k_progname);
	for (i = 0; i < (k_argc-1); i++) {
		kfree(k_argv[i]);
	}
	return ret;
}

/*
 * copyout_args - use, misuse, and abuse pointer arithmetic,
 * poetry in motion!
 *
 * @args in: 
 * 	k_argc - argument count
 * 	k_argv - pointer to an array of arguments
 * 
 * @args out:
 * 	usr_argv - pointer to the array of arguments in user space
 *
 * @args in/out:
 * 	usr_sp - in: Top of the user space stack
 * 			 out: New value of the stack pointer should point to
 * 	
 * 	return:
 * 		zero 		- on success
 * 		non-zero	- on failure
 */ 
int copyout_args(int k_argc, void** k_argv, uint32_t *usr_sp, uint32_t *usr_argv)
{
	uint32_t arg_len, dest_ptr;
	char *u_args_ptr; /* Keep it as char* for pointer arithmetic */
	int zero_blk = 0, i, ret;
	
	KASSERT(k_argv);
	KASSERT(usr_sp);
	KASSERT(usr_argv);

	*usr_argv = *usr_sp - (4 * (k_argc + 1));
	u_args_ptr = (char *)(*usr_argv);
	dest_ptr = (uint32_t)u_args_ptr;
	
	DEBUG(DB_EXEC, "argv points to %p\n", usr_argv);

	for (i = 0; i < k_argc; i++) {
		size_t len;
		arg_len = strlen(k_argv[i]) + 1;
		dest_ptr -= arg_len;
		/* align pointers to word length */
		ALIGN_PTR_TO_4BYTES(dest_ptr);
		ret = copyoutstr((void *)k_argv[i], (void *)dest_ptr, arg_len, &len);
		if (ret != 0) {
			return ret;
		}
		//TODO: Replace it with simple memcpy may be?
		ret = copyout((void *) &dest_ptr, (void *) u_args_ptr, 4);
		if (ret != 0) {
			return ret;
		}
		DEBUG(DB_EXEC, "argv[%d] points to %p\n", i, (uint32_t *)dest_ptr);
		u_args_ptr += 4;
	}
	//TODO:replace it with bzero?
	ret = copyout((void *) &(zero_blk), (void *) u_args_ptr, 4);
	//ret = copyout_zeros((usrptr_t)u_args_ptr, 4)
	if (ret != 0) {
		return ret;
	}
	/* stack pointer should point to the top of the stack */
	*usr_sp = dest_ptr;
	DEBUG(DB_EXEC, "sp points to %p\n", (uint32_t *)usr_sp);
	return 0;
}

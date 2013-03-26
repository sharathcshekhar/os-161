#include <types.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <syscall.h>
#include <process.h>
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

int sys_execv(userptr_t u_prog, userptr_t *u_argv, struct trapframe *tf)
{
	char *k_progname = kmalloc(MAX_PROGRAM_NAME);
	size_t len;
	int ret = 0, k_argc = 0;
	char *k_argv[MAX_ARGC]; //+1 for the NULL terminator
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
   	struct addrspace *old_as = NULL;
	int i = 0;

	ret = copyinstr(u_prog, k_progname, MAX_PROGRAM_NAME, &len);
	if (ret == ENAMETOOLONG) {
		return 1;
	}
	
	while ((u_argv[k_argc] != NULL) && (i < MAX_ARGC)) {
		k_argv[k_argc] = kmalloc(MAX_ARGV_LEN);
		copyinstr(u_argv[k_argc], k_argv[k_argc], MAX_ARGV_LEN, &len);
		k_argc++;
	}
	k_argv[k_argc] = NULL;

	/* Open the exec file. */
	ret = vfs_open(k_progname, O_RDONLY, 0, &v);
	if (ret) {
		return ret;
	}
	
	/* restore in case of error, destory or else */
	old_as = curthread->t_addrspace;
	/* This is redundant in dumbvm */
	as_activate(NULL);
    
	curthread->t_addrspace = as_create();
	/* Activate the new address space */
	as_activate(curthread->t_addrspace);
	
	ret = load_elf(v, &entrypoint);
	if (ret) {
		vfs_close(v);
		/* restore the old address space, destroy the new one */
		as_destroy(curthread->t_addrspace);
		curthread->t_addrspace = old_as;
		as_activate(curthread->t_addrspace);
		return ret;
	}

	/* Done with the file now. */
	vfs_close(v);
	
	ret = as_define_stack(curthread->t_addrspace, &stackptr);
	
	bzero(tf, sizeof(struct trapframe));	
	tf->tf_status = CST_IRQMASK | CST_IEp | CST_KUp;
	tf->tf_epc = entrypoint;
	tf->tf_a0 = k_argc;
	
	int argv_offset = stackptr - (1 + (4 * (k_argc + 1)));
	argv_offset = argv_offset - (argv_offset % 4);
	
	tf->tf_a1 = argv_offset;
	
	/*
	 * Stack pointer starts at USERSTACK - 0x7fffffff and grows downwards
	 * 
	 * addr				  		content
	 * ---------------------------------
	 * 7fffffff 			- argv[argc - 1] 
	 * 7ffffffb 			- argv[argc - 2]
	 * :						:
	 * :						:
	 * 7fffffff-4*(argc+1)	- argv[0]
	 *
	 * actual strings start from this address 
	 */
   	
	/* Keep it as char* for pointer arithmetic */
	char *u_args_ptr, *u_args;
	u_args_ptr = (char *) argv_offset;
	u_args = u_args_ptr;
	
	for (i = 0; i < k_argc; i++) {
		int arg_len = strlen(k_argv[i]) + 1;
		int dest_ptr = (int)(u_args - arg_len);
		//align pointers to word length
		dest_ptr = dest_ptr - (dest_ptr % 4);
		
		ret = copyoutstr((void *)k_argv[i], (void *)dest_ptr, arg_len, &len);
		KASSERT(ret == 0);
		ret = copyout((void *) &dest_ptr, (void *) u_args_ptr, 4);
		KASSERT(ret == 0);
		u_args_ptr += 4;
		u_args -= len;
	}
	
	int zero_blk = 0;
	ret = copyout((void *) &(zero_blk), (void *) u_args_ptr, 4);
	
	/* stack pointer should point to the top of the stack */	
	int allign_sp = (uint32_t) u_args;
	allign_sp = allign_sp - (allign_sp % 4);
	tf->tf_sp = allign_sp;
	
	as_destroy(old_as);
	return 0;
}

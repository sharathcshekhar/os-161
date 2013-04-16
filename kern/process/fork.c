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

static void 
create_user_process(void *args, unsigned long data);

struct fork_args {
	struct trapframe *tf;
	struct addrspace *as;
	struct process_struct *ps_table;
};

/*
 * Do most of the work in parent process itself, that way you 
 * don't even have to spawn a thread if there is a failure
 */
int sys_fork(struct trapframe *tf, int *child_pid)
{
	struct fork_args *args = kmalloc(sizeof(struct fork_args));
	struct child_process_list *child = NULL;
	/* TODO: name should be same of the parent */
	char name[] = "O_sweet_child";
	int ret;

	if (args == NULL) {
		return ENOMEM;
	}	
	
	if (pid_count == MAX_PID) {
		ret = ENPROC;
		goto clean_exit;
	}
	/* Make sure there is enough memory to add a child entry */
	child = kmalloc(sizeof(struct child_process_list));
	if (child == NULL) {
		ret = ENOMEM;
		goto clean_exit;
	}	
	args->ps_table = create_process_table();
	if (args->ps_table == NULL) {
		kfree(child);
		ret = ENOMEM;
		goto clean_exit;
	}
	args->tf = tf;
	ret = as_copy(curthread->t_addrspace, &args->as);
	/* copy was not going through resulting in panic in randcall test! */
	if (ret != 0) {
		kfree(child);
		destroy_process_table(args->ps_table);
		goto clean_exit;
	}
	
	ret = thread_fork(name /* thread name */,
			create_user_process /* thread function */,
			(void*)args /* thread arg */, 1 /* thread arg */,
			NULL);
	if (ret != 0) {
		as_destroy(args->as);
		destroy_process_table(args->ps_table);
		kfree(child);
		goto clean_exit;
	}
	lock_acquire(args->ps_table->status_lk);
	if (args->ps_table->status == PS_CREATE) {
		/* Child is being created, wait */
		cv_wait(args->ps_table->status_cv, args->ps_table->status_lk);
	}
	lock_release(args->ps_table->status_lk);
	
	/*
	 * if child_pid is -1, clean-up stuff and go back
	 * else return the pid to the user
	 */
	
	if (args->ps_table->status == PS_FAIL) {
		as_destroy(args->as);
		destroy_process_table(args->ps_table);
		kfree(child);
		*child_pid = -1;
		ret = ENOMEM;
		goto clean_exit;
	} /* else fork_status == E_CHILD_SUCCESS */

	if (curthread->process_table->children == NULL) {
		/* First child */
		curthread->process_table->children = child; 
		curthread->process_table->children->next = NULL;
		curthread->process_table->children->prev = NULL;
		curthread->process_table->children->child = args->ps_table;
	} else {
	   	/* Append new node to the list of children */	
		struct child_process_list *itr = curthread->process_table->children;
		while (itr->next != NULL) {
			itr = itr->next;
		}
		itr->next = child;
		itr->next->child = args->ps_table;
		itr->next->next = NULL;
		itr->next->prev = itr;
	}
	
	*child_pid = args->ps_table->pid;
	ret = 0;

clean_exit:	
	kfree(args);
	return ret;
}

static void
create_user_process(void *args, unsigned long data)
{
	/* We should now be in the new process, in kernel */
	(void)data; /* supress warning */
	struct trapframe *parent_tf = ((struct fork_args *)args)->tf;
	struct addrspace *new_as = ((struct fork_args *)args)->as;
	struct process_struct *process_table = ((struct fork_args *)args)->ps_table;
	int i, open_file_count;
	/* when returning to user, the trapframe has to be on the thread's stack */
	struct trapframe child_tf;
	/* Make a copy of the trapframe and then make modicications */
	KASSERT(parent_tf != NULL);
	memcpy((void *)&child_tf, (const void*)parent_tf, sizeof(struct trapframe));
	
	/* clone the file table */	
	open_file_count = process_table->father->open_file_count;
	for (i = 0; i < open_file_count; i++) {
		process_table->file_table[i] = process_table->father->file_table[i];	
		lock_acquire(process_table->file_table[i]->flock);
		process_table->file_table[i]->open_count++;
		lock_release(process_table->file_table[i]->flock);
		process_table->open_file_count++;
	}
	
	curthread->process_table = process_table;
	curthread->t_addrspace = new_as;
	
	/* Invalidate everything in the TLB */	
	as_activate(curthread->t_addrspace);
	
	lock_acquire(process_table->status_lk);
	/* Add error checks and indicate success only if really successful :) */
	process_table->status = PS_RUN;
	/* After waking the parent, he can go back to user mode and start executing */
	cv_signal(process_table->status_cv, process_table->status_lk);
	lock_release(process_table->status_lk);

	/* Increment the program counter for the child */
	child_tf.tf_epc += 4;
	/* return 0 to child */
	child_tf.tf_v0 = 0;
	/* Signal no error */
	child_tf.tf_a3 = 0;
	/* All set to enter user mode as a new process */

	//TODO: call as_complete_load() to prepare to goto user space
	mips_usermode(&child_tf);
}

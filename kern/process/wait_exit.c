#include <process.h>
#include <file.h>
#include <current.h>
#include <lib.h>
#include <vfs.h>
#include <syscall.h>
#include <thread.h>
#include <kern/wait.h>
#include <copyinout.h>

static void adopt_grand_children(struct child_process_list *children, struct process_struct *new_parent);
static void destroy_process_table(struct process_struct *ps_table);

/* Only Termination is supported. SIGSTOP is not supported */
int
sys_waitpid(pid_t *pid, userptr_t u_status, int options)
{
	struct child_process_list *itr = curthread->process_table->children;
	struct process_struct *child_ps_table = NULL;
	int k_status;
	int ret;

	ret = copyin(u_status, (void*)&k_status, 4);
	KASSERT(ret == 0);
	
	if (options != 0) {
		/* Not supported, report error */
		return 1;
	}	
	if (itr == NULL) {
		/* waiting with no kids! Error! */
		return 1;
	}
	
	/* delink the child from the list of children */
	while (itr != NULL){
		if (itr->child->pid == *pid) {
			child_ps_table = itr->child;
			if (itr->prev == NULL) {
				/* first node */
				curthread->process_table->children = itr->next;
				itr->next->prev = NULL;
			} else if (itr->next == NULL) {
				/* last node */
				itr->prev->next = NULL;
			} else {
				itr->prev->next = itr->next;
				itr->next->prev = itr->prev;
			}
			kfree(itr);
			break;
		}
		itr = itr->next;
	}
	
	if (child_ps_table == NULL) {
		/* not my child */
		return 1;
	}
	
	lock_acquire(child_ps_table->status_lk);
	/* 
	 * if the child has exited, return immediately,
	 * if not, wait for a signal from the child
	 */
	if ((child_ps_table->status != PS_ZTERM) ||
			(child_ps_table->status != PS_ZSTOP)) {
		cv_wait(child_ps_table->status_cv, child_ps_table->status_lk);
	}
	/* Child has called _exit(), clean up and return */
	lock_release(child_ps_table->status_lk);
	k_status = _MKWVAL(child_ps_table->exit_code) | __WEXITED;
	*pid = child_ps_table->pid;
	ret = copyout((void*)&k_status, u_status, 4);
	KASSERT(ret == 0);
	destroy_process_table(child_ps_table);
	return 0;
}

static void 
destroy_process_table(struct process_struct *ps_table)
{
	clear_pid(ps_table->pid);
	lock_destroy(ps_table->status_lk);
	cv_destroy(ps_table->status_cv);
	if (!ps_table->process_name) {	
		/* If process name is assigned, reclaim it */
		kfree(ps_table->process_name);
	}
	kfree(ps_table);
	return;
}

void
sys__exit(int exit_code)
{
	int i;
	struct global_file_handler *fh = NULL;
	
	/* 
	 * Assign all the children to it's grandfather
	 * 
	 * This table can be accessed by any of the children too
	 * and has to be protected by a global lock
	 */
	lock_acquire(global_ps_table_lk);
	adopt_grand_children(curthread->process_table->children,
				curthread->process_table->father);
	lock_release(global_ps_table_lk);
	
	/* clean up the file table and associated handlers */
	for (i = 0; i < MAX_FILES_PER_PROCESS; i++) {
		fh = curthread->process_table->file_table[i];
		lock_acquire(fh->flock);
		fh->open_count--;
		if (fh->open_count == 0) {
			vfs_close(fh->vnode);
			lock_release(fh->flock);
			lock_destroy(fh->flock);
			kfree(fh);
		} else {
			lock_release(fh->flock);
		}
	}
	kfree(curthread->process_table->file_table);
		
	curthread->process_table->exit_code = exit_code;
	lock_acquire(curthread->process_table->status_lk);
	curthread->process_table->status = PS_ZTERM;
	cv_signal(curthread->process_table->status_cv, 
			curthread->process_table->status_lk);
	lock_release(curthread->process_table->status_lk);
	
	/* 
	 * All status variables are set, kill the thread 
	 * The process table it self will be cleaned up the parent
	 *
	 * remove the reference to the current thread which will be
	 * destroyed
	 */
	
	curthread->process_table->thread = NULL;
	thread_exit();
	/* Rest in Peace */
}

/* This function should be done automically */
static void
adopt_grand_children(struct child_process_list *children,
			struct process_struct *new_parent)
{
	struct child_process_list *itr = children;
	/* Update the father for all the children */
	while (itr != NULL) {
		itr->child->father = new_parent;
		itr = itr->next;
	}
	/* Append the list of children to the father's existing child list */
	itr = new_parent->children;
	while (itr->next != NULL) {
		itr = itr->next;
	}
	
	itr->next = children;
	children->prev = itr;
	return;
}

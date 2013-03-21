#include <thread.h>
#include <file.h>
#include <types.h>

struct process_struct {
	pid_t pid;
	char *process_name;
	int status; /* Running, wait(), exit() */
	process_file_table *filetable;
	int open_files;
	struct thread *running_thread;
	struct process_struct *children;
	struct process_struct *father;
	/* Should use encoding used in wait.h */
	int exit_code;
	struct_cv *exit_signal;
	/*
	 * Scheduler realated variables
	 */ 
}

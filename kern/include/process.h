#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <file.h>
#include <types.h>
#include <synch.h>
#include <thread.h>

#define MAX_PID 1024

int get_pid(void);
void clear_pid(int pid);
void pid_init(void);
bool is_pid_in_use(pid_t pid);
struct process_struct* create_process_table(void);
void destroy_process_table(struct process_struct *ps_table);
void process_bootstrap(void);
int open_std_streams(struct global_file_handler **file_table);
int copyout_args(int k_argc, void** k_argv, uint32_t *usr_sp, uint32_t *usr_argv);
int __waitpid(pid_t *pid, struct process_struct *child_ps_table);

extern uint32_t pid_map[MAX_PID/(sizeof(int) * 8)];
extern struct lock *global_ps_table_lk;
extern struct lock *global_file_count_lk;
extern int pid_count;
extern int global_file_count;

/* States a process can be in */
typedef enum {
	PS_CREATE,	/* process is being created */
	PS_FAIL,	/* fork() has failed */
	PS_RUN,		/* running */
	PS_WAIT,	/* waiting for child */
	PS_ZSTOP,	/* Zombied by stop, waiting to be restarted/collected */
	PS_ZTERM,	/* Zombied, terminated, waiting to be collected */
} process_state_t;

struct process_struct {
	pid_t pid;
	char *process_name;
	struct thread *thread;
	process_state_t status; /* Running, wait(), exit() */
	/* Array of file pointers */
	struct global_file_handler **file_table;
	int open_file_count;
	struct child_process_list *children;
	struct process_struct *father;
	/* Should use encoding used in wait.h */
	int exit_code;
	struct cv *status_cv;
	struct lock *status_lk;
	/*
	 * Scheduler realated variables
	 */
};

struct child_process_list {
	struct process_struct *child;
	struct child_process_list *next;
	struct child_process_list *prev;
};

#endif

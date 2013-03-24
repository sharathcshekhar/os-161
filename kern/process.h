#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <file.h>
#include <types.h>
#include <synch.h>

#define MAX_PID 1024

int get_pid(void);
void clear_pid(int pid);
void pid_init(void);
struct process_struct* create_process_table(void);
int open_std_streams(struct global_file_handler **file_table);

extern struct thread *curthread;
extern uint32_t pid_map[MAX_PID/(sizeof(int) * 8)];
extern int pid_count;

/* States a process can be in */
typedef enum {
	PS_RUN,		/* running */
	PS_WAIT,		/* waiting for child */
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
	struct cv *exit_signal;
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

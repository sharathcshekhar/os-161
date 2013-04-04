#include <process.h>
#include <file.h>
#include <kern/fcntl.h>
#include <current.h>
#include <lib.h>
#include <vfs.h>
#include <syscall.h>

/* forward declaration to avoid cyclic dependency */
struct thread;

/* Global variables */
uint32_t pid_map[MAX_PID/(sizeof(int) * 8)];
int pid_count;;
int global_file_count;;
struct lock *global_ps_table_lk;
struct lock *global_pid_lk;
struct lock *global_file_count_lk;

/* local functions */
static int get_pid_index(int pid_map, int bit_len);
static int set_pid(int index, int offset);

void process_bootstrap(void)
{
	uint32_t i; /* to supress compiler warning */
	pid_count = 0;
	for (i = 0; i < (MAX_PID/(sizeof(int) * 8)); i++) {
		pid_map[i] = 0;
	}
	global_ps_table_lk = lock_create("global_process_table_lk");
	global_pid_lk = lock_create("global_pid_lk");
	global_file_count_lk = lock_create("global_file_count_lk");
	return;
}

void clear_pid(int pid)
{
	int index = pid/32;
	int bit_offset = pid % 32;
	int mask = 1 << bit_offset;
	lock_acquire(global_pid_lk);
	pid_map[index] &= (~mask);
	pid_count--;
	lock_release(global_pid_lk);
}

int get_pid(void) 
{
	int offset;
	int i;
	int pid;
	
	lock_acquire(global_pid_lk);
	if (pid_count == (MAX_PID-1)) {
		lock_release(global_pid_lk);
		return -1;
	}
	
	for (i = 0; i < (MAX_PID/32); i++) {
		if (pid_map[i] != 0xffffffff) {
			offset = get_pid_index(pid_map[i], 32);
			pid = set_pid(i, offset);
			lock_release(global_pid_lk);
			return pid;
		}
	}
	lock_release(global_pid_lk);
	return -1;
}

static int set_pid(int index, int offset)
{
   	pid_map[index] |= (1 << offset);
	pid_count++;
	return ((index * 32) + offset);
}

static int get_pid_index(int _pid_map, int bit_len)
{
	int index = 0;
	int mask = ~(~(0x0) << (bit_len/2));
	if (bit_len == 1) {
		return 0;
	}
	int lower_pid_map = _pid_map & mask; 
	if (lower_pid_map < mask) {
		/* free PID exists in the lower half */
		index = get_pid_index(lower_pid_map, bit_len/2);
	} else {
		/* free PID exists in the upper half */
		_pid_map >>= (bit_len/2);
		index = (bit_len / 2) + get_pid_index(_pid_map, bit_len/2);
	}
	return index;
}

struct process_struct*
create_process_table(void)
{
	struct process_struct *process;
	int i;
	process = kmalloc(sizeof(struct process_struct));
	process->process_name = NULL;
	process->pid = get_pid();
	process->thread = curthread;
	process->status = PS_CREATE;
	process->file_table = kmalloc(MAX_FILES_PER_PROCESS * sizeof(struct global_file_hanlder**));
	
	/* init the file table */
	for (i = 0; i < MAX_FILES_PER_PROCESS; i++) {
		process->file_table[i] = NULL;
	}
	process->open_file_count = 0;
	process->children = NULL;
	process->father = curthread->process_table;
	process->exit_code = 0;
	process->status_cv = cv_create("status_cv");
	process->status_lk = lock_create("status_lk");
	
	return process;
}

int open_std_streams(struct global_file_handler **file_table)
{
	struct vnode *std_in, *std_out, *std_err;
	char console[6];
	int ret = 0;
	
	KASSERT(file_table != NULL);
	
	/* Open STDIN */
	strcpy(console, "con:");
	ret = vfs_open(console, O_RDONLY, 0, &std_in);
	KASSERT(ret == 0);
	file_table[0] = kmalloc(sizeof(struct global_file_handler));
	file_table[0]->vnode = std_in;
	file_table[0]->offset = 0;
	file_table[0]->open_count++;
	file_table[0]->open_flags = O_RDONLY;
	/* Ref counts and locks migh not be required for std io streams */
	file_table[0]->flock = lock_create("file_handler_lk");
	
	/* Open STDOUT */
	strcpy(console, "con:");
	ret = vfs_open(console, O_WRONLY, 0, &std_out);
	KASSERT(ret == 0);
	file_table[1] = kmalloc(sizeof(struct global_file_handler));
	file_table[1]->vnode = std_out;
	file_table[1]->offset = 0;
	file_table[1]->open_count++;
	file_table[1]->open_flags = O_WRONLY;
	file_table[1]->flock = lock_create("file_handler_lk");
	
	/* Open STDERR */
	strcpy(console, "con:");
	ret = vfs_open(console, O_WRONLY, 0, &std_err);
	KASSERT(ret == 0);
	file_table[2] = kmalloc(sizeof(struct global_file_handler));
	file_table[2]->vnode = std_err;
	file_table[2]->offset = 0;
	file_table[2]->open_count++;
	file_table[2]->open_flags = O_WRONLY;
	file_table[2]->flock = lock_create("file_handler_lk");
	
	return 0;
}

/*
 * system call getpid()
 */ 
int sys_getpid(int *pid)
{
	*pid = curthread->process_table->pid;
	return 0;
}

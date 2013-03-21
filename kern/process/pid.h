#define MAX_PID 1024

int get_pid(void);
void clear_pid(int pid);
void pid_init(void);

extern int pid_map[MAXPID/(sizeof(int) * 8)];
extern int pid_count;

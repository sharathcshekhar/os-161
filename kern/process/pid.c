#include <pid.h>

static int get_pid_index(int pid_map, int bit_len);
static int set_pid(int index, int offset);

int pid_map[MAXPID/(sizeof(int) * 8)];
int pid_count = 0;

void clear_pid(int pid)
{
	int index = pid/32;
	int bit_offset = pid % 32;
	int mask = 1 << bit_offset;
	pid_map[index] &= (~mask);
	pid_count--;
}

int get_pid(void) 
{
	int offset;
	int i;
	int pid;
	
	if (pid_count == (MAX_PID-1)) {
		return -1;
	}
	
	for (i = 0; i < (MAX_PID/32); i++) {
		if (pid_map[i] != 0xffffffff) {
			offset = get_pid_index(pid_map[i], 32);
			pid = set_pid(i, offset);
			return pid;
		}
	}
	return -1;
}

static int set_pid(int index, int offset)
{
   	pid_map[index] &= (1 << offset);
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
	int tmp = _pid_map & mask; 
	if (tmp < mask) {
		/* free PID exists in the lower half */
		index = get_pid_index(tmp, bit_len/2);
	} else {
		/* free PID exists in the upper half */
		_pid_map >>= (bit_len/2);
		index = (bit_len / 2) + get_pid_index(_pid_map, bit_len/2);
	}
	return index;
}

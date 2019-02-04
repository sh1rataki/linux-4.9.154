#ifndef _LINUX_MEMTRACE_H
#define _LINUX_MEMTRACE_H

#include <linux/types.h>
#include <linux/sched.h>

extern pid_t pg_trace_pid;

struct memtrace_block {
	unsigned int    seq;
	unsigned long	access_flag;
	unsigned long dirty_flag;
	unsigned long rw_flag;
};

#define MAX_MEMTRACE_BLOCK 512

pid_t get_pg_trace_pid(void);
void set_pg_trace_pid(pid_t pid);
void set_mem_trace(struct task_struct *tsk, int flag);
void set_task_seq(struct task_struct *tsk, unsigned int seq);
unsigned int get_task_seq(struct task_struct *tsk);
void init_seq_number(void);
unsigned int get_seq_number(void);
unsigned int inc_seq_number(void);
void set_memtrace_block_sz(int sz);
//void mark_memtrace_block_accessed(unsigned long paddr);
void mark_memtrace_block(unsigned long paddr, unsigned long bir_ref, unsigned long bir_dir, unsigned long bir_rw);
void init_memtrace_blocks(void);
void kernel_mapping_ref(void);
void update_and_log_data(void);

#endif /* _LINUX_MEMTRACE_H */

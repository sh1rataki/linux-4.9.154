#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <asm/pgtable.h>
#include <linux/connector.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <asm/tlbflush.h>
#include <linux/types.h>
#include <asm/page.h>
#include <linux/kthread.h>
#include <linux/memtrace.h>

struct task_struct *memref_thr;
struct task_struct *tsk;
unsigned int seq;
struct mm_struct *k_mm;

static pid_t trace_pid = -1;
static int interval = 10;
static int memtrace_block_size = 2;

#define LIMIT 1024
int top = -1;
struct task_struct *stack[LIMIT];

module_param(trace_pid, int, 0664);
MODULE_PARM_DESC(trace_pid, "Pid of app to be traced");
module_param(interval, int, 0664);
MODULE_PARM_DESC(interval, "Sampling interval in milliseconds");
module_param(memtrace_block_size, int, 0664);
MODULE_PARM_DESC(memtrace_block_size, "Memory Block Size");

static int check_and_clear_task_pages(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->private;
	unsigned long pfn;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;
	unsigned long paddr;
	
	unsigned long bit_referenced = 0;
	unsigned long bit_dirty = 0;
	unsigned long bit_rw = 0;
	
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;
		if (!pte_present(ptent) || pte_none(*pte) || pte_huge(*pte))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/* this is where need to check if reference bit was set,
		 * if found to be set, make a note of it and then clear it
		 */
		if(ptep_test_and_clear_young(vma, addr, pte)) {
			ClearPageReferenced(page);
			bit_referenced = 1;
		}
		if(ptep_test_and_clear_dirty(vma, addr, pte)){
			ClearPageReferenced(page);
			bit_dirty = 1;
		}
		if (pte_write(*pte)) {
			bit_rw = 1;
		}
		pfn = pte_pfn(ptent);
		if(pfn_valid(pfn)) {
			if(bit_referenced||bit_dirty||bit_rw){
				paddr = pfn << PAGE_SHIFT;
				mark_memtrace_block(paddr, bit_referenced, bit_dirty, bit_rw);
			}
		}
		bit_referenced = 0;
		bit_dirty = 0;
		bit_rw = 0;
		}
	pte_unmap_unlock(pte - 1, ptl);
	return 0;
}

static void walk_task_pages(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	if (mm) {
		struct mm_walk walk_task_pages = {
			.pmd_entry = check_and_clear_task_pages,
			.mm = mm,
		};
		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			walk_task_pages.private = vma;
			if (!is_vm_hugetlb_page(vma)) ;
				walk_page_range(vma->vm_start, vma->vm_end,
						&walk_task_pages);
		}
		flush_tlb_mm(mm);
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
}

static int is_list_empty(void)
{
	if(top == -1)
		return 1;
	return 0;
}

static void insert_into_list(struct task_struct *v)
{
	if(top == LIMIT)
		return;
	top++;
	stack[top] = v;
}

static struct task_struct* del_from_list(void)
{
	struct task_struct *t;

	if(is_list_empty())
		return NULL;
	t = stack[top];
	top--;
	return t;
}

static void walk_tasks(struct task_struct *p)
{
	struct task_struct *t, *c;
	struct mm_struct *mm_task;

	if(!p)
		return;

	insert_into_list(p);

	while(!is_list_empty()) {
		c = del_from_list();
		set_mem_trace(c, 1);
		if(!thread_group_leader(c))
			continue;
		set_task_seq(c, seq);
		mm_task = get_task_mm(c);
		if(mm_task)
			walk_task_pages(mm_task);

		list_for_each_entry(t, &c->children, sibling)
			if(get_task_seq(t) != seq)
				insert_into_list(t);
	}
	return;
}

static int memref_thread(void *data)
{
	struct task_struct *task = data;

	while(!kthread_should_stop() && task) {
		seq = inc_seq_number();

		walk_tasks(task);
		update_and_log_data();
        kernel_mapping_ref();
		msleep(interval);
	}
	return 0;
}

static int memref_start(void)
{

	rcu_read_lock();
	set_pg_trace_pid(trace_pid);
	init_seq_number();
	set_memtrace_block_sz(memtrace_block_size);

	tsk = find_task_by_vpid(trace_pid);
	if(!tsk) {
		printk("No task with pid %d found \n", trace_pid);
		tsk = ERR_PTR(-ESRCH);
		return -EINVAL;
	}

	set_mem_trace(tsk, 1);
	rcu_read_unlock();
	memref_thr = kthread_create(memref_thread, tsk, "memref");
	wake_up_process(memref_thr);
	return 0;
}

static void memref_stop(void)
{
	if(memref_thr)
		kthread_stop(memref_thr);
	set_pg_trace_pid(-1);
	set_mem_trace(tsk, 0);
	return;
}

module_init(memref_start);
module_exit(memref_stop);
MODULE_LICENSE("GPL"); 

#include <asm/atomic.h>
#include <linux/memtrace.h>
#include <linux/module.h>
#include <linux/mm.h>

#define CREATE_TRACE_POINTS
#include <trace/events/memtrace.h>

/* Trace Unique identifier */
atomic_t trace_sequence_number;
pid_t pg_trace_pid;
int memtrace_block_sz;
int total_block_count;

#define MB_SHIFT	20

/* TODO: Dynamically allocate this array depending on the amount of memory
 * present on the system
 */
struct memtrace_block memtrace_block_accessed[MAX_MEMTRACE_BLOCK+1];

/* App being traced */
pid_t get_pg_trace_pid(void)
{
	return pg_trace_pid;
}
EXPORT_SYMBOL_GPL(get_pg_trace_pid);

void set_pg_trace_pid(pid_t pid)
{
	pg_trace_pid = pid;
}
EXPORT_SYMBOL_GPL(set_pg_trace_pid);

void set_mem_trace(struct task_struct *tsk, int flag)
{
	tsk->mem_trace = flag;
}
EXPORT_SYMBOL_GPL(set_mem_trace);

void set_task_seq(struct task_struct *tsk, unsigned int seq)
{
	tsk->seq = seq;
}
EXPORT_SYMBOL_GPL(set_task_seq);

unsigned int get_task_seq(struct task_struct *tsk)
{
	return (tsk->seq);
}
EXPORT_SYMBOL_GPL(get_task_seq);

void init_seq_number(void)
{
	return (atomic_set(&trace_sequence_number, 0));
}
EXPORT_SYMBOL_GPL(init_seq_number);

unsigned int get_seq_number(void)
{
	return atomic_read(&trace_sequence_number);
}
EXPORT_SYMBOL_GPL(get_seq_number);

unsigned int inc_seq_number(void)
{
	return (atomic_inc_return(&trace_sequence_number));
}
EXPORT_SYMBOL_GPL(inc_seq_number);

void set_memtrace_block_sz(int sz)
{
	memtrace_block_sz = sz;
	total_block_count = (totalram_pages << PAGE_SHIFT) / (memtrace_block_sz << MB_SHIFT );
}
EXPORT_SYMBOL_GPL(set_memtrace_block_sz);

#define PTE_LEVEL_MULT (PAGE_SIZE)
#define PMD_LEVEL_MULT (PTRS_PER_PTE * PTE_LEVEL_MULT)
#define PUD_LEVEL_MULT (PTRS_PER_PMD * PMD_LEVEL_MULT)
#define PGD_LEVEL_MULT (PTRS_PER_PUD * PUD_LEVEL_MULT)

static void walk_k_pte_level(pmd_t pmd, unsigned long addr)
 {
 	pte_t *pte;
 	int i, ret;
	unsigned long pfn;
	struct page *pg;

	pte = (pte_t*) pmd_page_vaddr(pmd);

 	for (i = 0; i < PTRS_PER_PTE; i++, pte++) {
 		if(!pte_present(*pte) && pte_none(*pte) && pte_huge(*pte))
			continue;

		pfn = pte_pfn(*pte);
		if(pfn_valid(pfn) && pte_young(*pte)) {
			ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
						(unsigned long *) &pte->pte);
			if (ret) {
				pg = pfn_to_page(pfn);
				ClearPageReferenced(pg);
				mark_memtrace_block_accessed(pfn << PAGE_SHIFT);
			}
		}
 	}
}

#if PTRS_PER_PMD > 1

static void walk_k_pmd_level(pud_t pud, unsigned long addr)
 {
 	pmd_t *pmd;
 	int i;

 	pmd = (pmd_t *) pud_page_vaddr(pud);

 	for (i = 0; i < PTRS_PER_PMD; i++) {

 		if(!pmd_none(*pmd) && pmd_present(*pmd) && !pmd_large(*pmd))
			walk_k_pte_level(*pmd, addr + i * PMD_LEVEL_MULT);

 		pmd++;
 	}
 }

#else
#define walk_pmd_level(p,a) walk_pte_level(__pmd(pud_val(p)),a)
#define pud_none(a)  pmd_none(__pmd(pud_val(a)))
#define pud_large(a) pmd_large(__pmd(pud_val(a)))
#endif

#if PTRS_PER_PUD > 1

static void walk_k_pud_level(pgd_t pgd, unsigned long addr)
 {
 	pud_t *pud;
 	int i;

 	pud = (pud_t *) pgd_page_vaddr(pgd);

 	for (i = 0; i < PTRS_PER_PUD; i++) {

 		if(!pud_none(*pud) && pud_present(*pud) && !pud_large(*pud))
 			walk_k_pmd_level(*pud, addr + i * PUD_LEVEL_MULT);
 		pud++;
 	}
 }

#else
#define walk_pud_level(p,a) walk_pmd_level(__pud(pgd_val(p)),a)
#define pgd_none(a)  pud_none(__pud(pgd_val(a)))
#define pgd_large(a) pud_large(__pud(pgd_val(a)))
#endif

void kernel_mapping_ref(void)
{
 	pgd_t *pgd;
 	int i;

        pgd = (pgd_t *) &init_level4_pgt;

 	for (i=0; i < PTRS_PER_PGD; i++) {

 		if(!pgd_none(*pgd) && pgd_present(*pgd) && !pgd_large(*pgd)) {
 			walk_k_pud_level(*pgd, i * PGD_LEVEL_MULT);
		}
 		pgd++;
 	}
}
EXPORT_SYMBOL_GPL(kernel_mapping_ref);

void mark_memtrace_block_accessed(unsigned long paddr)
 {
	int memtrace_block;
	unsigned long paddr_mb;

	paddr_mb = paddr >> MB_SHIFT;

	memtrace_block = ((int) paddr_mb/memtrace_block_sz) + 1;
	memtrace_block_accessed[memtrace_block].seq = get_seq_number();
	memtrace_block_accessed[memtrace_block].access_flag = 1;
}
EXPORT_SYMBOL_GPL(mark_memtrace_block_accessed);

void update_and_log_data(void)
{
 	int i;
	unsigned int seq;
	unsigned long base_addr, access_flag;

	for (i = 1; i <= total_block_count; i++) {
		seq = memtrace_block_accessed[i].seq;
		base_addr = i * memtrace_block_sz;
		access_flag = memtrace_block_accessed[i].access_flag;
		/*
		 *  Log trace data
		 *  Can modify to dump only blocks that have been marked
		 *  accessed
		 */
        trace_memtrace(seq, base_addr, access_flag);
		memtrace_block_accessed[i].access_flag = 0;
 	}

	return;
}
EXPORT_SYMBOL_GPL(update_and_log_data); 

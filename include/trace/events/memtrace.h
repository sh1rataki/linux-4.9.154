#undef TRACE_SYSTEM
#define TRACE_SYSTEM memtrace

#include <linux/tracepoint.h>

#if !defined(_TRACE_MEMTRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEMTRACE_H

TRACE_EVENT(memtrace,
	TP_PROTO(unsigned int seq, unsigned long base, unsigned long access_flag),
	TP_ARGS(seq, base, access_flag),
	TP_STRUCT__entry(
		__field(	unsigned int ,	seq		)
		__field(	unsigned long,	base		)
		__field(	unsigned long,	access_flag	)
	),
	TP_fast_assign(
		__entry->seq		= seq;
		__entry->base		= base;
		__entry->access_flag	= access_flag;
	),
	TP_printk("%u %lu %lu", __entry->seq, __entry->base, __entry->access_flag)
);

#endif /* _TRACE_MEMTRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h> 

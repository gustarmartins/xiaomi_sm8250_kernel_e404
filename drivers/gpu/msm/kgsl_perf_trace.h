/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal KGSL performance tracepoints retained for device diagnostics.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kgsl_perf

#if !defined(_KGSL_PERF_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _KGSL_PERF_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(kgsl_gpu_frequency,
	TP_PROTO(const char *device_name, unsigned int pwrlevel,
		 unsigned int freq, unsigned int prev_pwrlevel,
		 unsigned int prev_freq),
	TP_ARGS(device_name, pwrlevel, freq, prev_pwrlevel, prev_freq),
	TP_STRUCT__entry(__string(device_name, device_name)
		__field(unsigned int, pwrlevel)
		__field(unsigned int, freq)
		__field(unsigned int, prev_pwrlevel)
		__field(unsigned int, prev_freq)
	),
	TP_fast_assign(__assign_str(device_name, device_name);
		__entry->pwrlevel = pwrlevel;
		__entry->freq = freq;
		__entry->prev_pwrlevel = prev_pwrlevel;
		__entry->prev_freq = prev_freq;
	),
	TP_printk("d_name=%s pwrlevel=%u freq=%u prev_pwrlevel=%u prev_freq=%u",
		  __get_str(device_name), __entry->pwrlevel, __entry->freq,
		  __entry->prev_pwrlevel, __entry->prev_freq)
);

#endif /* _KGSL_PERF_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE kgsl_perf_trace

#include <trace/define_trace.h>

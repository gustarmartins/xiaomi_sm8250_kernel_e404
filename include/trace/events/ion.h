/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ion

#if !defined(_TRACE_ION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ION_H

#include <linux/dma-direction.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#define DEV_NAME_NONE "None"

DECLARE_EVENT_CLASS(ion_dma_map_cmo_class,

	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, unsigned long map_attrs,
		 enum dma_data_direction dir),

	TP_ARGS(dev, name, cached, hlos_accessible, map_attrs, dir),

	TP_STRUCT__entry(
		__string(dev_name, dev ? dev_name(dev) : DEV_NAME_NONE)
		__string(name, name)
		__field(bool, cached)
		__field(bool, hlos_accessible)
		__field(unsigned long, map_attrs)
		__field(enum dma_data_direction, dir)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev ? dev_name(dev) : DEV_NAME_NONE);
		__assign_str(name, name);
		__entry->cached = cached;
		__entry->hlos_accessible = hlos_accessible;
		__entry->map_attrs = map_attrs;
		__entry->dir = dir;
	),

	TP_printk("dev=%s name=%s cached=%d access=%d map_attrs=0x%lx dir=%d",
		__get_str(dev_name),
		__get_str(name),
		__entry->cached,
		__entry->hlos_accessible,
		__entry->map_attrs,
		__entry->dir)
);

DEFINE_EVENT(ion_dma_map_cmo_class, ion_dma_map_cmo_apply,

	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, unsigned long map_attrs,
		 enum dma_data_direction dir),

	TP_ARGS(dev, name, cached, hlos_accessible, map_attrs, dir)
);

DEFINE_EVENT(ion_dma_map_cmo_class, ion_dma_map_cmo_skip,

	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, unsigned long map_attrs,
		 enum dma_data_direction dir),

	TP_ARGS(dev, name, cached, hlos_accessible, map_attrs, dir)
);

DEFINE_EVENT(ion_dma_map_cmo_class, ion_dma_unmap_cmo_apply,

	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, unsigned long map_attrs,
		 enum dma_data_direction dir),

	TP_ARGS(dev, name, cached, hlos_accessible, map_attrs, dir)
);

DEFINE_EVENT(ion_dma_map_cmo_class, ion_dma_unmap_cmo_skip,

	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, unsigned long map_attrs,
		 enum dma_data_direction dir),

	TP_ARGS(dev, name, cached, hlos_accessible, map_attrs, dir)
);

DECLARE_EVENT_CLASS(ion_access_cmo_class,

	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped),

	TP_STRUCT__entry(
		__string(dev_name, dev ? dev_name(dev) : DEV_NAME_NONE)
		__string(name, name)
		__field(bool, cached)
		__field(bool, hlos_accessible)
		__field(enum dma_data_direction, dir)
		__field(bool, only_mapped)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev ? dev_name(dev) : DEV_NAME_NONE);
		__assign_str(name, name);
		__entry->cached = cached;
		__entry->hlos_accessible = hlos_accessible;
		__entry->dir = dir;
		__entry->only_mapped = only_mapped;
	),

	TP_printk("dev=%s name=%s cached=%d access=%d dir=%d, only_mapped=%d",
		  __get_str(dev_name),
		  __get_str(name),
		  __entry->cached,
		  __entry->hlos_accessible,
		  __entry->dir,
		  __entry->only_mapped)
);

DEFINE_EVENT(ion_access_cmo_class, ion_begin_cpu_access_cmo_apply,
	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped)
);

DEFINE_EVENT(ion_access_cmo_class, ion_begin_cpu_access_cmo_skip,
	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped)
);

DEFINE_EVENT(ion_access_cmo_class, ion_begin_cpu_access_notmapped,
	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped)
);

DEFINE_EVENT(ion_access_cmo_class, ion_end_cpu_access_cmo_apply,
	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped)
);

DEFINE_EVENT(ion_access_cmo_class, ion_end_cpu_access_cmo_skip,
	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped)
);

DEFINE_EVENT(ion_access_cmo_class, ion_end_cpu_access_notmapped,
	TP_PROTO(const struct device *dev, const char *name,
		 bool cached, bool hlos_accessible, enum dma_data_direction dir,
		 bool only_mapped),

	TP_ARGS(dev, name, cached, hlos_accessible, dir, only_mapped)
);

TRACE_EVENT(ion_page_pool_buddy_alloc,

	TP_PROTO(const void *pool, unsigned int order, bool cached,
		 unsigned long pool_pages, bool pool_requested,
		 bool lock_contended, int errorno),

	TP_ARGS(pool, order, cached, pool_pages, pool_requested,
		lock_contended, errorno),

	TP_STRUCT__entry(
		__field(const void *, pool)
		__field(unsigned int, order)
		__field(bool, cached)
		__field(unsigned long, pool_pages)
		__field(bool, pool_requested)
		__field(bool, lock_contended)
		__field(int, errorno)
	),

	TP_fast_assign(
		__entry->pool = pool;
		__entry->order = order;
		__entry->cached = cached;
		__entry->pool_pages = pool_pages;
		__entry->pool_requested = pool_requested;
		__entry->lock_contended = lock_contended;
		__entry->errorno = errorno;
	),

	TP_printk("pool=%p order=%u cached=%d pool_pages=%lu requested=%d contended=%d errorno=%d",
		  __entry->pool, __entry->order, __entry->cached,
		  __entry->pool_pages, __entry->pool_requested,
		  __entry->lock_contended, __entry->errorno)
);

TRACE_EVENT(ion_page_pool_refill,

	TP_PROTO(const void *pool, unsigned int order, bool cached,
		 unsigned long before_pages, unsigned long after_pages,
		 unsigned long target_pages, int reason, s64 duration_ns),

	TP_ARGS(pool, order, cached, before_pages, after_pages,
		target_pages, reason, duration_ns),

	TP_STRUCT__entry(
		__field(const void *, pool)
		__field(unsigned int, order)
		__field(bool, cached)
		__field(unsigned long, before_pages)
		__field(unsigned long, after_pages)
		__field(unsigned long, target_pages)
		__field(int, reason)
		__field(s64, duration_ns)
	),

	TP_fast_assign(
		__entry->pool = pool;
		__entry->order = order;
		__entry->cached = cached;
		__entry->before_pages = before_pages;
		__entry->after_pages = after_pages;
		__entry->target_pages = target_pages;
		__entry->reason = reason;
		__entry->duration_ns = duration_ns;
	),

	TP_printk("pool=%p order=%u cached=%d before_pages=%lu after_pages=%lu target_pages=%lu reason=%s duration_ns=%lld",
		  __entry->pool, __entry->order, __entry->cached,
		  __entry->before_pages, __entry->after_pages,
		  __entry->target_pages,
		  __print_symbolic(__entry->reason,
			{ 0, "target" },
			{ 1, "deferred" },
			{ 2, "alloc_failed" }),
		  (long long)__entry->duration_ns)
);
#endif /* _TRACE_ION_H */

#include <trace/define_trace.h>

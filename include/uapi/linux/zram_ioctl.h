/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_ZRAM_IOCTL_H
#define _UAPI_LINUX_ZRAM_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define ZRAM_ANDROID_IOC_VERSION 2

struct zram_android_ioc_data_process_writeback {
	__aligned_u64 pidfd;
	__u64 written_bytes;
};

/* Legacy command-1 layout; its size is part of the Android ABI. */
struct zram_android_ioc_data {
	union {
		struct zram_android_ioc_data_process_writeback process_writeback;
	} data;
};

struct zram_android_ioc_process_range_writeback {
	__aligned_u64 pidfd;
	__u64 start_addr;
	__u64 size;
	__u64 next_addr;
	__u64 written_bytes;
};

struct zram_android_ioc_process_prefetch {
	__aligned_u64 pidfd;
};

/* Flags returned in struct zram_android_ioc_slot_record::flags. */
#define ZRAM_ANDROID_SLOT_SAME			(1U << 0)
#define ZRAM_ANDROID_SLOT_WRITEBACK		(1U << 1)
#define ZRAM_ANDROID_SLOT_HUGE			(1U << 2)
#define ZRAM_ANDROID_SLOT_IDLE			(1U << 3)
#define ZRAM_ANDROID_SLOT_RECOMPRESSED		(1U << 4)
#define ZRAM_ANDROID_SLOT_INCOMPRESSIBLE	(1U << 5)
/* The page is resident in zram but still owns a reusable backing block. */
#define ZRAM_ANDROID_SLOT_PREFETCHED_BACKING	(1U << 6)

/* One swapped virtual page mapped by the queried process. */
struct zram_android_ioc_slot_record {
	__u64 vaddr;
	__u64 swap_offset;
	/* CLOCK_BOOTTIME nanoseconds; zero means unavailable. */
	__u64 access_time_ns;
	__u32 object_size;
	__u32 flags;
	__u32 comp_priority;
	__u32 reserved;
};

/*
 * Bounded, cursor-based process attribution query.  records_ptr points to an
 * array of record_capacity records.  max_scan_bytes limits eligible mapped
 * virtual memory examined by one call; next_addr is zero at end of the mm.
 * Page counters describe mappings, so a shared swap slot mapped more than once
 * is intentionally counted more than once.
 */
struct zram_android_ioc_process_range_query {
	__aligned_u64 pidfd;
	__u64 start_addr;
	__u64 max_scan_bytes;
	__u64 next_addr;
	__u64 scanned_bytes;
	__aligned_u64 records_ptr;
	__u32 record_capacity;
	__u32 record_count;
	__u64 swapped_pages;
	__u64 stored_bytes;
	__u64 writeback_pages;
	__u64 recompressed_pages;
	__u64 same_pages;
	__u64 huge_pages;
	__u64 idle_pages;
	__u64 incompressible_pages;
	__u64 prefetched_backing_pages;
	__u64 reserved[4];
};

#define ZRAM_ANDROID_IOC_MAGIC 0xBB
#define ZRAM_ANDROID_IOC_PROCESS_WRITEBACK \
	_IOWR(ZRAM_ANDROID_IOC_MAGIC, 1, struct zram_android_ioc_data)
#define ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK \
	_IOWR(ZRAM_ANDROID_IOC_MAGIC, 2, \
	      struct zram_android_ioc_process_range_writeback)
#define ZRAM_ANDROID_IOC_PROCESS_PREFETCH \
	_IOW(ZRAM_ANDROID_IOC_MAGIC, 3, struct zram_android_ioc_process_prefetch)
#define ZRAM_ANDROID_IOC_GET_VERSION _IO(ZRAM_ANDROID_IOC_MAGIC, 4)
#define ZRAM_ANDROID_IOC_PROCESS_RANGE_QUERY \
	_IOWR(ZRAM_ANDROID_IOC_MAGIC, 5, \
	      struct zram_android_ioc_process_range_query)

#endif /* _UAPI_LINUX_ZRAM_IOCTL_H */

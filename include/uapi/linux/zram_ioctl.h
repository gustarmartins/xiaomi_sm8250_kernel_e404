/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_ZRAM_IOCTL_H
#define _UAPI_LINUX_ZRAM_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define ZRAM_ANDROID_IOC_VERSION 1

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

#define ZRAM_ANDROID_IOC_MAGIC 0xBB
#define ZRAM_ANDROID_IOC_PROCESS_WRITEBACK \
	_IOWR(ZRAM_ANDROID_IOC_MAGIC, 1, struct zram_android_ioc_data)
#define ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK \
	_IOWR(ZRAM_ANDROID_IOC_MAGIC, 2, \
	      struct zram_android_ioc_process_range_writeback)
#define ZRAM_ANDROID_IOC_PROCESS_PREFETCH \
	_IOW(ZRAM_ANDROID_IOC_MAGIC, 3, struct zram_android_ioc_process_prefetch)
#define ZRAM_ANDROID_IOC_GET_VERSION _IO(ZRAM_ANDROID_IOC_MAGIC, 4)

#endif /* _UAPI_LINUX_ZRAM_IOCTL_H */

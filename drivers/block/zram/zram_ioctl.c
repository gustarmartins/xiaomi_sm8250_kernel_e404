// SPDX-License-Identifier: GPL-2.0-or-later

#define KMSG_COMPONENT "zram_ioctl"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <uapi/linux/zram_ioctl.h>

#include "zram_drv.h"
#include "zram_ioctl.h"

int zram_ioctl(struct block_device *bdev, fmode_t mode,
	       unsigned int cmd, unsigned long arg)
{
	if (cmd == ZRAM_ANDROID_IOC_GET_VERSION)
		return ZRAM_ANDROID_IOC_VERSION;
	return -ENOIOCTLCMD;
}

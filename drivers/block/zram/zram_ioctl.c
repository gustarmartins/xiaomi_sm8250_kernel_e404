// SPDX-License-Identifier: GPL-2.0-or-later

#define KMSG_COMPONENT "zram_ioctl"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/blkdev.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagewalk.h>
#include <linux/pid.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/uaccess.h>
#include <uapi/linux/zram_ioctl.h>

#include "zram_drv.h"
#include "zram_ioctl.h"

#define NR_PAGES_UNLIMITED U64_MAX

struct zram_process_walk_private {
	struct zram *zram;
	struct zram_pp_ctl *pp_ctl;
	u64 nr_remaining_pages;
	unsigned long next_addr;
	bool stopped;
};

static bool can_do_file_pageout(struct vm_area_struct *vma)
{
	if (!vma->vm_file)
		return false;
	return inode_owner_or_capable(file_inode(vma->vm_file)) ||
		inode_permission(file_inode(vma->vm_file), MAY_WRITE) == 0;
}

static struct task_struct *zram_pidfd_get_task(unsigned int pidfd)
{
	struct task_struct *task;
	unsigned int flags;
	struct pid *pid;

	pid = pidfd_get_pid(pidfd, &flags);
	if (IS_ERR(pid))
		return ERR_CAST(pid);
	task = get_pid_task(pid, PIDTYPE_TGID);
	put_pid(pid);
	return task ?: ERR_PTR(-ESRCH);
}

static int zram_process_walker(pmd_t *pmd, unsigned long start,
			       unsigned long end, struct mm_walk *walk)
{
	struct zram_process_walk_private *private = walk->private;
	struct zram *zram = private->zram;
	struct vm_area_struct *vma = walk->vma;
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	unsigned long addr;

	if (pmd_trans_huge(*pmd) || pmd_devmap(*pmd) || pmd_bad(*pmd))
		return 0;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		struct swap_info_struct *sis;
		spinlock_t *ptl;
		swp_entry_t entry;
		pte_t *ptep, pte;
		unsigned long index;
		int ret;

		if (!private->nr_remaining_pages) {
			private->next_addr = addr;
			private->stopped = true;
			return -EAGAIN;
		}

		ptep = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
		if (!ptep)
			break;
		pte = READ_ONCE(*ptep);
		if (!is_swap_pte(pte)) {
			pte_unmap_unlock(ptep, ptl);
			continue;
		}
		entry = pte_to_swp_entry(pte);
		if (unlikely(non_swap_entry(entry)) || swap_duplicate(entry)) {
			pte_unmap_unlock(ptep, ptl);
			continue;
		}
		pte_unmap_unlock(ptep, ptl);

		/*
		 * 4.19 has no get_swap_device()/put_swap_device() lifetime API.
		 * A valid swap PTE owns a slot reference; duplicate it while the
		 * PTE lock still stabilizes that observation, then release the
		 * extra reference after inspecting the zram slot.  This makes
		 * swapoff unuse this reference before it can tear the device down.
		 */
		sis = swp_swap_info(entry);
		if (!sis || !(READ_ONCE(sis->flags) & SWP_USED) || !sis->bdev ||
		    !sis->bdev->bd_disk ||
		    sis->bdev->bd_disk->private_data != zram)
			goto put_entry;

		index = swp_offset(entry);
		if (index >= nr_pages)
			goto put_entry;

		ret = zram_scan_slot_for_writeback(zram, index,
						   private->pp_ctl);
		if (ret && ret != -ERANGE) {
			swap_free(entry);
			return ret;
		}
		if (private->nr_remaining_pages != NR_PAGES_UNLIMITED)
			private->nr_remaining_pages--;
put_entry:
		swap_free(entry);
	}

	cond_resched();
	return 0;
}

static const struct mm_walk_ops zram_walk_ops = {
	.pmd_entry = zram_process_walker,
};

static int zram_ioctl_process_scan(struct zram *zram,
		struct zram_android_ioc_process_range_writeback *range,
		struct zram_pp_ctl *pp_ctl)
{
	struct zram_process_walk_private private = {
		.zram = zram,
		.pp_ctl = pp_ctl,
		.nr_remaining_pages = NR_PAGES_UNLIMITED,
	};
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
	int ret = 0;

	if (range->pidfd > UINT_MAX || range->start_addr > ULONG_MAX)
		return -EINVAL;
	start_addr = range->start_addr;
	if (range->size)
		private.nr_remaining_pages =
			DIV_ROUND_UP_ULL(range->size, PAGE_SIZE);

	task = zram_pidfd_get_task(range->pidfd);
	if (IS_ERR(task))
		return PTR_ERR(task);
	mm = get_task_mm(task);
	put_task_struct(task);
	if (!mm)
		return -ESRCH;

	if (start_addr >= mm->task_size || !IS_ALIGNED(start_addr, PAGE_SIZE)) {
		ret = -EINVAL;
		goto put_mm;
	}

	down_read(&mm->mmap_sem);
	for (vma = find_vma(mm, start_addr); vma; vma = vma->vm_next) {
		unsigned long start = max(vma->vm_start, start_addr);

		if (!vma_is_anonymous(vma) &&
		    (!can_do_file_pageout(vma) && (vma->vm_flags & VM_MAYSHARE)))
			continue;

		ret = walk_page_range(mm, start, vma->vm_end, &zram_walk_ops,
				      &private);
		if (private.stopped) {
			ret = 0;
			break;
		}
		if (ret)
			break;
	}
	up_read(&mm->mmap_sem);

	range->next_addr = private.stopped ? private.next_addr : 0;
put_mm:
	mmput(mm);
	return ret;
}

static int zram_ioctl_process_writeback(struct zram *zram,
		struct zram_android_ioc_process_range_writeback *range)
{
	struct zram_pp_ctl *pp_ctl = NULL;
	struct zram_wb_ctl *wb_ctl = NULL;
	int ret;

	if (!capable(CAP_SYS_NICE))
		return -EPERM;

	down_read(&zram->init_lock);
	if (!zram->disksize) {
		ret = -EINVAL;
		goto unlock;
	}
	if (!zram->backing_dev) {
		ret = -ENODEV;
		goto unlock;
	}
	if (atomic_xchg(&zram->pp_in_progress, 1)) {
		ret = -EAGAIN;
		goto unlock;
	}

	pp_ctl = zram_pp_ctl_alloc();
	if (!pp_ctl) {
		ret = -ENOMEM;
		goto clear_progress;
	}
	wb_ctl = zram_wb_ctl_alloc(zram);
	if (!wb_ctl) {
		ret = -ENOMEM;
		goto clear_progress;
	}

	ret = zram_ioctl_process_scan(zram, range, pp_ctl);
	if (!ret)
		ret = zram_writeback_slots(zram, pp_ctl, wb_ctl);
	range->written_bytes = zram_wb_processed_bytes(wb_ctl);

clear_progress:
	zram_wb_ctl_free(wb_ctl);
	zram_pp_ctl_free(zram, pp_ctl);
	atomic_set(&zram->pp_in_progress, 0);
unlock:
	up_read(&zram->init_lock);
	return ret;
}

int zram_ioctl(struct block_device *bdev, fmode_t mode,
	       unsigned int cmd, unsigned long arg)
{
	struct zram *zram = bdev->bd_disk->private_data;
	void __user *argp = (void __user *)arg;
	int ret = -ENOIOCTLCMD;

	(void)mode;

	if (cmd == ZRAM_ANDROID_IOC_GET_VERSION)
		return ZRAM_ANDROID_IOC_VERSION;

	if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK) {
		struct zram_android_ioc_process_range_writeback range;

		if (copy_from_user(&range, argp, sizeof(range)))
			return -EFAULT;
		ret = zram_ioctl_process_writeback(zram, &range);
		if (copy_to_user(argp, &range, sizeof(range)))
			ret = -EFAULT;
	} else if (cmd == ZRAM_ANDROID_IOC_PROCESS_WRITEBACK) {
		struct zram_android_ioc_process_range_writeback range = { 0 };
		struct zram_android_ioc_data legacy;

		if (copy_from_user(&legacy, argp, sizeof(legacy)))
			return -EFAULT;
		range.pidfd = legacy.data.process_writeback.pidfd;
		ret = zram_ioctl_process_writeback(zram, &range);
		legacy.data.process_writeback.written_bytes =
			range.written_bytes;
		if (copy_to_user(argp, &legacy, sizeof(legacy)))
			ret = -EFAULT;
	}

	return ret;
}

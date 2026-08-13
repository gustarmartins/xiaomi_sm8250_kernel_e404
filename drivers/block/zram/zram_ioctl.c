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
#include <linux/vmalloc.h>
#include <uapi/linux/zram_ioctl.h>

#include "zram_drv.h"
#include "zram_ioctl.h"

#define NR_PAGES_UNLIMITED U64_MAX
/* Bound mmap_sem read-side hold time on the old linked-VMA implementation. */
#define ZRAM_WALK_BATCH_BYTES (16UL << 20)
#define ZRAM_QUERY_MAX_SCAN_BYTES (256ULL << 20)
#define ZRAM_QUERY_MAX_RECORDS 4096U

struct zram_process_walk_private {
	struct zram *zram;
	struct zram_pp_ctl *pp_ctl;
	unsigned int cmd;
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

		if (private->cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK)
			ret = zram_scan_slot_for_writeback(zram, index,
							   private->pp_ctl);
		else
			ret = zram_scan_slot_for_prefetch(zram, index,
							  private->pp_ctl);
		if (ret && ret != -ERANGE) {
			swap_free(entry);
			return ret;
		}
		if (private->cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK &&
		    private->nr_remaining_pages != NR_PAGES_UNLIMITED)
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

struct zram_process_query_private {
	struct zram *zram;
	struct zram_android_ioc_process_range_query *query;
	struct zram_android_ioc_slot_record *records;
	unsigned long next_addr;
	bool stopped;
};

static u32 zram_snapshot_uapi_flags(const struct zram_slot_snapshot *snapshot)
{
	u32 flags = 0;

	if (snapshot->same)
		flags |= ZRAM_ANDROID_SLOT_SAME;
	if (snapshot->writeback)
		flags |= ZRAM_ANDROID_SLOT_WRITEBACK;
	if (snapshot->huge)
		flags |= ZRAM_ANDROID_SLOT_HUGE;
	if (snapshot->idle)
		flags |= ZRAM_ANDROID_SLOT_IDLE;
	if (snapshot->comp_priority)
		flags |= ZRAM_ANDROID_SLOT_RECOMPRESSED;
	if (snapshot->incompressible)
		flags |= ZRAM_ANDROID_SLOT_INCOMPRESSIBLE;
	if (snapshot->prefetched_backing)
		flags |= ZRAM_ANDROID_SLOT_PREFETCHED_BACKING;

	return flags;
}

static void zram_query_account_record(
	struct zram_android_ioc_process_range_query *query,
	const struct zram_android_ioc_slot_record *record)
{
	query->swapped_pages++;
	query->stored_bytes += record->object_size;
	if (record->flags & ZRAM_ANDROID_SLOT_WRITEBACK)
		query->writeback_pages++;
	if (record->flags & ZRAM_ANDROID_SLOT_RECOMPRESSED)
		query->recompressed_pages++;
	if (record->flags & ZRAM_ANDROID_SLOT_SAME)
		query->same_pages++;
	if (record->flags & ZRAM_ANDROID_SLOT_HUGE)
		query->huge_pages++;
	if (record->flags & ZRAM_ANDROID_SLOT_IDLE)
		query->idle_pages++;
	if (record->flags & ZRAM_ANDROID_SLOT_INCOMPRESSIBLE)
		query->incompressible_pages++;
	if (record->flags & ZRAM_ANDROID_SLOT_PREFETCHED_BACKING)
		query->prefetched_backing_pages++;
}

static int zram_process_query_walker(pmd_t *pmd, unsigned long start,
				     unsigned long end, struct mm_walk *walk)
{
	struct zram_process_query_private *private = walk->private;
	struct zram_android_ioc_process_range_query *query = private->query;
	struct zram *zram = private->zram;
	struct vm_area_struct *vma = walk->vma;
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	unsigned long addr;

	if (pmd_trans_huge(*pmd) || pmd_devmap(*pmd) || pmd_bad(*pmd))
		return 0;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		struct zram_android_ioc_slot_record *record;
		struct zram_slot_snapshot snapshot;
		struct swap_info_struct *sis;
		/* Stabilizes the PTE while its swap reference is duplicated. */
		spinlock_t *ptl;
		swp_entry_t entry;
		pte_t *ptep, pte;
		unsigned long index;
		int ret;

		if (query->record_count >= query->record_capacity) {
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

		sis = swp_swap_info(entry);
		if (!sis || !(READ_ONCE(sis->flags) & SWP_USED) || !sis->bdev ||
		    !sis->bdev->bd_disk ||
		    sis->bdev->bd_disk->private_data != zram)
			goto put_entry;

		index = swp_offset(entry);
		if (index >= nr_pages)
			goto put_entry;

		ret = zram_get_slot_snapshot(zram, index, &snapshot);
		if (ret == -ENOENT)
			goto put_entry;
		if (ret) {
			swap_free(entry);
			return ret;
		}

		record = &private->records[query->record_count++];
		memset(record, 0, sizeof(*record));
		record->vaddr = addr;
		record->swap_offset = index;
		record->access_time_ns = snapshot.access_time_ns;
		record->object_size = snapshot.object_size;
		record->comp_priority = snapshot.comp_priority;
		record->flags = zram_snapshot_uapi_flags(&snapshot);
		zram_query_account_record(query, record);
put_entry:
		swap_free(entry);
	}

	cond_resched();
	return 0;
}

static const struct mm_walk_ops zram_query_walk_ops = {
	.pmd_entry = zram_process_query_walker,
};

static int zram_ioctl_process_query(struct zram *zram,
			struct zram_android_ioc_process_range_query *query)
{
	struct zram_android_ioc_slot_record *records = NULL;
	struct zram_process_query_private private = {
		.zram = zram,
		.query = query,
	};
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long cursor, start_addr;
	u64 scanned = 0;
	int ret = 0;

	if (!capable(CAP_SYS_NICE))
		return -EPERM;
	if (query->pidfd > UINT_MAX || query->start_addr > ULONG_MAX)
		return -EINVAL;
	if (!query->max_scan_bytes ||
	    query->max_scan_bytes > ZRAM_QUERY_MAX_SCAN_BYTES ||
	    !IS_ALIGNED(query->max_scan_bytes, PAGE_SIZE) ||
	    !query->record_capacity ||
	    query->record_capacity > ZRAM_QUERY_MAX_RECORDS ||
	    !query->records_ptr)
		return -EINVAL;

	start_addr = query->start_addr;
	records = kvcalloc(query->record_capacity, sizeof(*records),
			   GFP_KERNEL);
	if (!records)
		return -ENOMEM;
	private.records = records;
	down_read(&zram->init_lock);
	if (!zram->disksize) {
		ret = -EINVAL;
		goto unlock_init;
	}

	query->next_addr = 0;
	query->scanned_bytes = 0;
	query->record_count = 0;
	query->swapped_pages = 0;
	query->stored_bytes = 0;
	query->writeback_pages = 0;
	query->recompressed_pages = 0;
	query->same_pages = 0;
	query->huge_pages = 0;
	query->idle_pages = 0;
	query->incompressible_pages = 0;
	query->prefetched_backing_pages = 0;
	memset(query->reserved, 0, sizeof(query->reserved));

	task = zram_pidfd_get_task(query->pidfd);
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto unlock_init;
	}
	mm = get_task_mm(task);
	put_task_struct(task);
	if (!mm) {
		ret = -ESRCH;
		goto unlock_init;
	}

	if (start_addr >= mm->task_size || !IS_ALIGNED(start_addr, PAGE_SIZE)) {
		ret = -EINVAL;
		goto put_mm;
	}

	for (cursor = start_addr;
	     cursor < mm->task_size && scanned < query->max_scan_bytes;) {
		unsigned long end, start;
		u64 remaining;

		down_read(&mm->mmap_sem);
		vma = find_vma(mm, cursor);
		if (!vma) {
			up_read(&mm->mmap_sem);
			cursor = mm->task_size;
			break;
		}

		start = max(vma->vm_start, cursor);
		remaining = query->max_scan_bytes - scanned;
		end = start + min_t(u64, remaining, ZRAM_WALK_BATCH_BYTES);
		if (end < start || end > vma->vm_end)
			end = vma->vm_end;

		ret = walk_page_range(mm, start, end, &zram_query_walk_ops,
				      &private);
		if (private.stopped) {
			scanned += private.next_addr - start;
			cursor = private.next_addr;
			ret = 0;
			up_read(&mm->mmap_sem);
			break;
		}
		scanned += end - start;
		cursor = end;
		up_read(&mm->mmap_sem);
		if (ret)
			break;
		cond_resched();
	}

	query->scanned_bytes = scanned;
	if (!ret && cursor < mm->task_size)
		query->next_addr = cursor;
put_mm:
	mmput(mm);
unlock_init:
	up_read(&zram->init_lock);
	if (!ret && query->record_count &&
	    copy_to_user(u64_to_user_ptr(query->records_ptr), records,
			 query->record_count * sizeof(*records)))
		ret = -EFAULT;
	kvfree(records);
	return ret;
}

static int zram_ioctl_process_scan(struct zram *zram, unsigned int cmd,
		u64 pidfd, struct zram_android_ioc_process_range_writeback *range,
		struct zram_pp_ctl *pp_ctl)
{
	struct zram_process_walk_private private = {
		.zram = zram,
		.pp_ctl = pp_ctl,
		.cmd = cmd,
		.nr_remaining_pages = NR_PAGES_UNLIMITED,
	};
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long cursor, start_addr;
	int ret = 0;

	if (pidfd > UINT_MAX)
		return -EINVAL;
	start_addr = 0;
	if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK &&
	    range->start_addr > ULONG_MAX)
		return -EINVAL;
	if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK)
		start_addr = range->start_addr;
	if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK && range->size)
		private.nr_remaining_pages =
			DIV_ROUND_UP_ULL(range->size, PAGE_SIZE);

	task = zram_pidfd_get_task(pidfd);
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

	/*
	 * Maple Tree/per-VMA locking is not available on this 4.19 tree.  Re-find
	 * the VMA for every bounded chunk so munmap/exit can make progress between
	 * chunks without retaining a stale linked-list VMA pointer.
	 */
	for (cursor = start_addr; cursor < mm->task_size; ) {
		unsigned long end, start;

		down_read(&mm->mmap_sem);
		vma = find_vma(mm, cursor);
		if (!vma) {
			up_read(&mm->mmap_sem);
			break;
		}

		start = max(vma->vm_start, cursor);
		end = start + ZRAM_WALK_BATCH_BYTES;
		if (end < start || end > vma->vm_end)
			end = vma->vm_end;

		if (!vma_is_anonymous(vma) &&
		    (!can_do_file_pageout(vma) && (vma->vm_flags & VM_MAYSHARE))) {
			cursor = vma->vm_end;
			up_read(&mm->mmap_sem);
			cond_resched();
			continue;
		}

		ret = walk_page_range(mm, start, end, &zram_walk_ops, &private);
		cursor = end;
		up_read(&mm->mmap_sem);
		if (private.stopped) {
			ret = 0;
			break;
		}
		if (ret)
			break;
		cond_resched();
	}

	if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK)
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

	ret = zram_ioctl_process_scan(zram,
				      ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK,
				      range->pidfd, range, pp_ctl);
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

static int zram_ioctl_process_prefetch(struct zram *zram,
		struct zram_android_ioc_process_prefetch *prefetch)
{
	struct zram_pp_ctl *pp_ctl = NULL;
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
	/* Keep reset, writeback and recompression out until every bio finishes. */
	if (atomic_xchg(&zram->pp_in_progress, 1)) {
		ret = -EAGAIN;
		goto unlock;
	}

	pp_ctl = zram_pp_ctl_alloc();
	if (!pp_ctl) {
		ret = -ENOMEM;
		goto clear_progress;
	}

	ret = zram_ioctl_process_scan(zram, ZRAM_ANDROID_IOC_PROCESS_PREFETCH,
				      prefetch->pidfd, NULL, pp_ctl);
	if (!ret)
		ret = zram_prefetch_slots(zram, pp_ctl);

clear_progress:
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

	if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_QUERY) {
		struct zram_android_ioc_process_range_query query;

		if (copy_from_user(&query, argp, sizeof(query)))
			return -EFAULT;
		ret = zram_ioctl_process_query(zram, &query);
		if (copy_to_user(argp, &query, sizeof(query)))
			ret = -EFAULT;
	} else if (cmd == ZRAM_ANDROID_IOC_PROCESS_RANGE_WRITEBACK) {
		struct zram_android_ioc_process_range_writeback range;

		if (copy_from_user(&range, argp, sizeof(range)))
			return -EFAULT;
		ret = zram_ioctl_process_writeback(zram, &range);
		if (copy_to_user(argp, &range, sizeof(range)))
			ret = -EFAULT;
	} else if (cmd == ZRAM_ANDROID_IOC_PROCESS_PREFETCH) {
		struct zram_android_ioc_process_prefetch prefetch;

		if (copy_from_user(&prefetch, argp, sizeof(prefetch)))
			return -EFAULT;
		ret = zram_ioctl_process_prefetch(zram, &prefetch);
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

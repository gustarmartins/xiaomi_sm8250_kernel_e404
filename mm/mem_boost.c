// SPDX-License-Identifier: GPL-2.0
/* Short, explicit userspace hints; expiry never needs a userspace reset. */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/mem_boost.h>
#include <linux/mm.h>
#include <linux/seqlock.h>
#include <linux/sysfs.h>
#include <linux/vmstat.h>

#define MEM_BOOST_DURATION (5 * HZ)

static DEFINE_SEQLOCK(mem_boost_lock);
static unsigned int mem_boost_mode;
static unsigned long mem_boost_expires;

static unsigned int mem_boost_current_mode(void)
{
	unsigned int seq, mode;
	unsigned long expires;

	do {
		seq = read_seqbegin(&mem_boost_lock);
		mode = mem_boost_mode;
		expires = mem_boost_expires;
	} while (read_seqretry(&mem_boost_lock, seq));

	/* Readers must not clear a newer hint when an older hint expires. */
	return mode && time_before(jiffies, expires) ? mode : 0;
}

bool mem_boost_active(void)
{
	return mem_boost_current_mode() >= 2;
}
EXPORT_SYMBOL_GPL(mem_boost_active);

bool mem_boost_file_reclaim(void)
{
	unsigned long ram, floor_mb, file;

	if (!mem_boost_active())
		return false;

	ram = totalram_pages >> (30 - PAGE_SHIFT);
	floor_mb = ram >= 4 ? 500 : ram >= 3 ? 400 : ram >= 2 ? 300 : 200;
	file = global_node_page_state(NR_ACTIVE_FILE) +
	       global_node_page_state(NR_INACTIVE_FILE);
	return file > (floor_mb << (20 - PAGE_SHIFT));
}

static ssize_t mem_boost_mode_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", mem_boost_current_mode());
}

static ssize_t mem_boost_mode_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int mode;
	unsigned long flags;
	bool acquire = sysfs_streq(buf, "try2");

	if (acquire)
		mode = 2;
	else if (kstrtouint(buf, 10, &mode) || mode > 3)
		return -EINVAL;

	write_seqlock_irqsave(&mem_boost_lock, flags);
	if (acquire && mem_boost_mode &&
	    time_before(jiffies, mem_boost_expires)) {
		write_sequnlock_irqrestore(&mem_boost_lock, flags);
		return -EBUSY;
	}
	mem_boost_mode = mode;
	mem_boost_expires = jiffies + MEM_BOOST_DURATION;
	write_sequnlock_irqrestore(&mem_boost_lock, flags);
	return count;
}

static struct kobj_attribute mem_boost_mode_attr =
	__ATTR(mem_boost_mode, 0600, mem_boost_mode_show, mem_boost_mode_store);

static ssize_t mem_boost_contract_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "e404-launch-io-v1 5000\n");
}

static struct kobj_attribute mem_boost_contract_attr =
	__ATTR(mem_boost_contract, 0444, mem_boost_contract_show, NULL);

static struct attribute *mem_boost_attrs[] = {
	&mem_boost_mode_attr.attr,
	&mem_boost_contract_attr.attr,
	NULL,
};

static const struct attribute_group mem_boost_group = {
	.attrs = mem_boost_attrs,
};

static int __init mem_boost_init(void)
{
	return sysfs_create_group(mm_kobj, &mem_boost_group);
}
subsys_initcall(mem_boost_init);

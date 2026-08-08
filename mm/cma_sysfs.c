// SPDX-License-Identifier: GPL-2.0
/*
 * Read-only CMA SysFS interface.
 *
 * Adapted for the 4.19 E404 tree from the upstream interface introduced by
 * Minchan Kim. Counters are pages, not allocation calls, so large camera and
 * display requests remain visible even when they are infrequent.
 */

#include <linux/cma.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "cma.h"

#define CMA_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

void cma_sysfs_account_success_pages(struct cma *cma,
				     unsigned long nr_pages)
{
	atomic64_add(nr_pages, &cma->nr_pages_succeeded);
}

void cma_sysfs_account_fail_pages(struct cma *cma, unsigned long nr_pages)
{
	atomic64_add(nr_pages, &cma->nr_pages_failed);
}

void cma_sysfs_account_release_pages(struct cma *cma,
				     unsigned long nr_pages)
{
	atomic64_add(nr_pages, &cma->nr_pages_released);
}

static inline struct cma *cma_from_kobj(struct kobject *kobj)
{
	return container_of(kobj, struct cma_kobject, kobj)->cma;
}

static ssize_t alloc_pages_success_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct cma *cma = cma_from_kobj(kobj);

	return scnprintf(buf, PAGE_SIZE, "%lld\n",
			 (long long)atomic64_read(&cma->nr_pages_succeeded));
}
CMA_ATTR_RO(alloc_pages_success);

static ssize_t alloc_pages_fail_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct cma *cma = cma_from_kobj(kobj);

	return scnprintf(buf, PAGE_SIZE, "%lld\n",
			 (long long)atomic64_read(&cma->nr_pages_failed));
}
CMA_ATTR_RO(alloc_pages_fail);

static ssize_t release_pages_success_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	struct cma *cma = cma_from_kobj(kobj);

	return scnprintf(buf, PAGE_SIZE, "%lld\n",
			 (long long)atomic64_read(&cma->nr_pages_released));
}
CMA_ATTR_RO(release_pages_success);

static struct attribute *cma_attrs[] = {
	&alloc_pages_success_attr.attr,
	&alloc_pages_fail_attr.attr,
	&release_pages_success_attr.attr,
	NULL,
};

static void cma_kobj_release(struct kobject *kobj)
{
	struct cma *cma = cma_from_kobj(kobj);

	kfree(cma->cma_kobj);
	cma->cma_kobj = NULL;
}

static struct kobj_type cma_ktype = {
	.release = cma_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = cma_attrs,
};

static int __init cma_sysfs_init(void)
{
	struct kobject *cma_kobj_root;
	struct cma_kobject *cma_kobj;
	struct cma *cma;
	int i, err;

	cma_kobj_root = kobject_create_and_add("cma", mm_kobj);
	if (!cma_kobj_root)
		return -ENOMEM;

	for (i = 0; i < cma_area_count; i++) {
		cma_kobj = kzalloc(sizeof(*cma_kobj), GFP_KERNEL);
		if (!cma_kobj) {
			err = -ENOMEM;
			goto out;
		}

		cma = &cma_areas[i];
		cma->cma_kobj = cma_kobj;
		cma_kobj->cma = cma;
		err = kobject_init_and_add(&cma_kobj->kobj, &cma_ktype,
					   cma_kobj_root, "%s", cma->name);
		if (err) {
			kobject_put(&cma_kobj->kobj);
			goto out;
		}
	}

	return 0;

out:
	while (--i >= 0) {
		cma = &cma_areas[i];
		kobject_put(&cma->cma_kobj->kobj);
	}
	kobject_put(cma_kobj_root);
	return err;
}
subsys_initcall(cma_sysfs_init);

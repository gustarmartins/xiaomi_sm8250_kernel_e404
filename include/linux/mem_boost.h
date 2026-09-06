/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEM_BOOST_H
#define _LINUX_MEM_BOOST_H

#include <linux/types.h>
#include <linux/kernel.h>

bool mem_boost_active(void);
bool mem_boost_file_reclaim(void);

static inline unsigned long mem_boost_readahead_pages(unsigned long pages)
{
	return mem_boost_active() ? min(pages, 8UL) : pages;
}

#endif

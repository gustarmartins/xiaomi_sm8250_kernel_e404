#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Compile the real readahead paths and check launch windows and recovery.

The mock page-cache backend records submissions, not device I/O latency.
Optional argument: a pre-clamp revision whose window assertions must fail.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[4]


def function(text, name):
    match = re.search(r'^(?:static (?:inline )?)?(?:unsigned long|int|struct file \*)\s*\n?' + name + r'\(', text, re.M)
    assert match, name
    start = text.index('{', match.end())
    end, depth = start + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned long pgoff_t;
#define PAGE_SHIFT 12
#define VM_RAND_READ 1
#define VM_SEQ_READ 2
#define MMAP_LOTSAMISS 100
#define min(a,b) ((a)<(b)?(a):(b))
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
struct backing_dev_info { unsigned long io_pages; };
struct address_space { struct backing_dev_info *host; };
struct file_ra_state { unsigned long ra_pages, start, size, async_size, prev_pos, mmap_miss; };
struct file { struct file_ra_state f_ra; struct address_space *f_mapping; };
struct vm_area_struct { struct file *vm_file; unsigned long vm_flags; };
struct vm_fault { struct vm_area_struct *vma; pgoff_t pgoff; };
static bool boosted;
static unsigned long submitted, history;
static bool mem_boost_active(void) { return boosted; }
static unsigned long roundup_pow_of_two(unsigned long n) { unsigned long v=1; while(v<n)v*=2;return v; }
static struct backing_dev_info *inode_to_bdi(struct backing_dev_info *bdi) { return bdi; }
static pgoff_t page_cache_next_miss(struct address_space *m,pgoff_t start,unsigned long n) { return start+2; }
static pgoff_t count_history_pages(struct address_space *m,pgoff_t offset,unsigned long max) { return min(history,max); }
static unsigned long ra_submit(struct file_ra_state *ra,struct address_space *m,struct file *f) { submitted=ra->size;return submitted; }
static unsigned long __do_page_cache_readahead(struct address_space *m,struct file *f,pgoff_t offset,unsigned long count,unsigned long lookahead) { submitted=count;return count; }
static struct file *maybe_unlock_mmap_for_io(struct vm_fault *vmf,struct file *pin) { return pin; }
'''
bridge = r'''
static void page_cache_sync_readahead(struct address_space *m,struct file_ra_state *ra,struct file *f,pgoff_t offset,unsigned long count) {
    ondemand_readahead(m,ra,f,false,offset,count);
}
'''
suffix = r'''
int main(void) {
    struct backing_dev_info bdi={.io_pages=1024};struct address_space m={.host=&bdi};
    struct file f={.f_ra={.ra_pages=128},.f_mapping=&m};
    struct vm_area_struct vma={.vm_file=&f};struct vm_fault vmf={.vma=&vma,.pgoff=100};
    do_sync_mmap_readahead(&vmf);assert(submitted==128 && f.f_ra.ra_pages==128);
    boosted=true;do_sync_mmap_readahead(&vmf);
    assert(submitted==8 && f.f_ra.start==96 && f.f_ra.async_size==2 && f.f_ra.ra_pages==128);
    vma.vm_flags=VM_RAND_READ;submitted=0;do_sync_mmap_readahead(&vmf);assert(submitted==0);
    vma.vm_flags=VM_SEQ_READ;do_sync_mmap_readahead(&vmf);assert(submitted<=8);
    f.f_ra=(struct file_ra_state){.ra_pages=128};
    ondemand_readahead(&m,&f.f_ra,&f,false,0,4096);assert(submitted==8 && f.f_ra.ra_pages==128);
    /* A marker laid down before the boost must not restore a large window. */
    f.f_ra=(struct file_ra_state){.ra_pages=128,.start=64,.size=128,.async_size=32};
    ondemand_readahead(&m,&f.f_ra,&f,true,160,128);assert(submitted==8);
    f.f_ra=(struct file_ra_state){.ra_pages=128};
    ondemand_readahead(&m,&f.f_ra,&f,true,1000,128);assert(submitted<=8);
    /* A small random demand read still submits all requested pages. */
    f.f_ra=(struct file_ra_state){.ra_pages=128};history=0;
    ondemand_readahead(&m,&f.f_ra,&f,false,1000,3);assert(submitted==3);
    history=8;ondemand_readahead(&m,&f.f_ra,&f,false,2000,1);assert(submitted<=8);
    f.f_ra=(struct file_ra_state){.ra_pages=4};
    ondemand_readahead(&m,&f.f_ra,&f,false,0,1);assert(submitted<=4);
    boosted=false;f.f_ra=(struct file_ra_state){.ra_pages=128};
    ondemand_readahead(&m,&f.f_ra,&f,false,0,4096);assert(submitted==1024);
    vma.vm_flags=0;do_sync_mmap_readahead(&vmf);assert(submitted==128);
    assert(f.f_ra.ra_pages==128);
    puts("PASS: launch windows, old markers, I/O-size override, random demand, and expiry recovery");
}
'''
helper = function((root/'include/linux/mem_boost.h').read_text(), 'mem_boost_readahead_pages')
for revision in [None] + sys.argv[1:]:
    def read(path):
        if revision:
            return subprocess.check_output(['git', '-C', str(root), 'show', revision+':'+path], text=True)
        return (root/path).read_text()
    ra = read('mm/readahead.c')
    body = ''.join(function(ra, n) for n in ('get_init_ra_size', 'get_next_ra_size', 'try_context_readahead', 'ondemand_readahead'))
    mmap = function(read('mm/filemap.c'), 'do_sync_mmap_readahead')
    with tempfile.TemporaryDirectory() as temp:
        c = pathlib.Path(temp)/'test.c'
        exe = c.with_suffix('')
        c.write_text(prefix+helper+body+bridge+mmap+suffix)
        subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
        assert (result.returncode == 0) == (revision is None), (revision, result.stdout, result.stderr)
        print((revision or 'current')+': '+(result.stdout.strip() if revision is None else 'expected unclamped-window assertion failure'))

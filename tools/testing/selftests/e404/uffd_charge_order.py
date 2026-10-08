#!/usr/bin/env python3
"""Compile actual UFFD install/copy functions with deterministic VM fixtures.

Exercises full pagevec drains, PTE publication and cancellation. Fixture locks
and counters do not establish kernel concurrency or live workload correctness.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import re


def function(text, name):
    match = re.search(r"^(?:static )?int " + name + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise ValueError(name)
    opening = text.index("{", match.start())
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end] + "\n"

ROOT = Path(__file__).resolve().parents[4]
PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#define VM_WRITE 1
#define VM_SHARED 2
#define GFP_HIGHUSER_MOVABLE 0
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define __user
#define unlikely(x) (x)
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
typedef unsigned long pgoff_t;
typedef int pte_t;
typedef int pmd_t;
typedef int spinlock_t;
struct mem_cgroup { int id; };
struct inode { unsigned long size; };
struct file { struct inode *f_inode; };
struct mm_struct { int unused; };
struct vm_area_struct { unsigned long vm_flags; int vm_page_prot; bool shmem; struct file *vm_file; };
struct page { struct mem_cgroup *charge; bool mapping, cache, lru; };
static struct mem_cgroup app={42};
static struct page allocated;
static pte_t slot;
static int occupancy, root_credit, app_credit, commits, cancels, releases, publications, uncharged_publications;
static bool queued, copy_fail, charge_fail;
#define page_mapping(p) ((p)->cache)
#define mk_pte(p,v) 1
#define pte_mkdirty(p) (p)
#define pte_mkwrite(p) (p)
#define pte_offset_map_lock(mm,pmd,addr,ptl) (&slot)
#define vma_is_shmem(v) ((v)->shmem)
#define linear_page_index(v,a) ((a)/PAGE_SIZE)
#define i_size_read(i) ((i)->size)
#define pte_none(p) (!(p))
#define page_add_file_rmap(p,c) ((p)->mapping=true)
#define page_add_new_anon_rmap(p,v,a,c) ((p)->mapping=true)
#define inc_mm_counter(mm,c) ((void)0)
#define mm_counter(p) 0
#define update_mmu_cache(v,a,p) ((void)0)
#define pte_unmap_unlock(p,l) ((void)0)
#define alloc_page_vma(g,v,a) (&allocated)
#define kmap_atomic(p) ((void *)(p))
#define kunmap_atomic(p) ((void)0)
#define copy_from_user(d,s,n) (copy_fail ? 1 : 0)
#define flush_dcache_page(p) ((void)0)
#define __SetPageUptodate(p) ((void)0)
static void mem_cgroup_commit_charge(struct page *p, struct mem_cgroup *c, bool l, bool compound) {
 assert(p->mapping && !p->charge); p->charge=c; commits++;
}
static void drain(struct page *p) {
 if (!queued) return;
 if (p->charge) app_credit++; else root_credit++;
 p->lru=true; queued=false; occupancy=0;
}
static void lru_cache_add_active_or_unevictable(struct page *p, struct vm_area_struct *v) {
 queued=true; if (++occupancy==15) drain(p);
}
static void set_pte_at(struct mm_struct *mm, unsigned long a, pte_t *p, pte_t val) {
 publications++; if (!allocated.charge) uncharged_publications++; *p=val;
}
static int mem_cgroup_try_charge(struct page *p,struct mm_struct *mm,int g,struct mem_cgroup **c,bool b) {
 *c=&app; return charge_fail;
}
static void mem_cgroup_cancel_charge(struct page *p,struct mem_cgroup *c,bool b) {cancels++;}
static void put_page(struct page *p) {releases++;}
'''
MAIN = r'''
int main(void) {
 struct mm_struct mm={0}; pmd_t pmd=0;
 struct vm_area_struct v={.vm_flags=VM_WRITE};
 for(int n=0;n<15;n++) {
  memset(&allocated,0,sizeof(allocated)); slot=0; occupancy=n; queued=false;
  root_credit=app_credit=commits=cancels=publications=uncharged_publications=0;
  struct page *saved=NULL;
  assert(mcopy_atomic_pte(&mm,&pmd,&v,0,0,&saved)==0);
  drain(&allocated);
  assert(commits==1 && cancels==0 && publications==1);
  if (EXPECT_BROKEN) {
   assert(uncharged_publications==1);
   assert(root_credit==(n==14));
  } else {
   assert(uncharged_publications==0 && root_credit==0 && app_credit==1);
  }
 }
 /* Occupied PTE fails before charge commit, insertion and publication. */
 memset(&allocated,0,sizeof(allocated)); slot=1; queued=false;
 commits=cancels=publications=releases=0;
 struct page *saved=NULL;
 assert(mcopy_atomic_pte(&mm,&pmd,&v,0,0,&saved)==-EEXIST);
 assert(commits==0 && cancels==1 && publications==0 && releases==1 && !queued);
 slot=0; copy_fail=true; commits=cancels=releases=0; saved=NULL;
 assert(mcopy_atomic_pte(&mm,&pmd,&v,0,0,&saved)==-ENOENT);
 assert(saved==&allocated && commits==0 && cancels==0 && releases==0);
 copy_fail=false; charge_fail=true; saved=NULL;
 assert(mcopy_atomic_pte(&mm,&pmd,&v,0,0,&saved)==-ENOMEM);
 assert(commits==0 && cancels==0 && releases==1);
 /* Shared helper callers already own a charge; never commit it twice. */
 charge_fail=false;
 struct inode inode={.size=PAGE_SIZE}; struct file file={.f_inode=&inode};
 v.shmem=true; v.vm_file=&file;
 for(int fresh=0;fresh<2;fresh++) {
  allocated=(struct page){.charge=&app,.mapping=true,.cache=true};
  slot=0; occupancy=14; queued=false;
  commits=publications=uncharged_publications=root_credit=app_credit=0;
  assert(INSTALL_CACHE==0);
  assert(commits==0 && publications==1 && uncharged_publications==0);
  assert(root_credit==0 && app_credit==fresh);
 }
 inode.size=0; slot=0; commits=publications=0;
 int fresh=1;
 assert(INSTALL_CACHE==-EFAULT);
 assert(commits==0 && publications==0);
 puts(EXPECT_BROKEN ? "REPRODUCED: uncharged PTE publication; full pagevec credits root" : "PASS: 15 pagevec occupancies preserve charge before LRU/PTE; collision/copy/charge failures preserve cancellation");
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'mm/userfaultfd.c')
    parser.add_argument('--expect-broken',action='store_true')
    args=parser.parse_args()
    source=args.source.read_text()
    install=function(source,'mfill_atomic_install_pte')
    extra=', NULL' if 'struct mem_cgroup *memcg' in install else ''
    cache_call='mfill_atomic_install_pte(&mm,&pmd,&v,0,&allocated,fresh'+extra+')'
    code=PREFIX+install+function(source,'mcopy_atomic_pte')+MAIN.replace('INSTALL_CACHE',cache_call)
    with tempfile.TemporaryDirectory(prefix='uffd-charge-') as tmp:
        p=Path(tmp); (p/'test.c').write_text(code)
        subprocess.run(['clang','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-g',f'-DEXPECT_BROKEN={int(args.expect_broken)}',str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
if __name__=='__main__': main()

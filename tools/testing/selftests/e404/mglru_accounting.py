#!/usr/bin/env python3
"""Exercise the actual synchronous MGLRU rmap promotion path.

The VM/PTE fixtures include the state after a 4.19 swap fault charges an
isolated, previously uncharged swapcache page. They verify that neither same-
owner nor foreign-owner promotions leave deferred generation deltas behind.
Queued activation drains execute the source-extracted __activate_page() and
MGLRU add/delete helpers. Locking and list plumbing remain fixtures: these
deterministic cases do not establish real kernel concurrency correctness.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def function(text, name):
    import re
    match = re.search(r"^(?:static |inline |__always_inline )*(?:void|int|bool) "
                      + name + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise ValueError(name)
    opening = text.index("{", match.start())
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end] + "\n"


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define unlikely(x) (x)
#define _RET_IP_ 0UL
#define lru_gen_record_transition(l,p,o,n,t,r) ((void)0)
#define BIT(n) (1UL<<(n))
#define MAX_NR_GENS 4
#define MIN_NR_GENS 2
#define LRU_GEN_ANON 0
#define ANON_AND_FILE 2
#define MAX_NR_ZONES 1
#define MIN_LRU_BATCH 32
#define PTRS_PER_PTE 32
#define PAGEVEC_SIZE 15
#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define PMD_MASK (~(512UL * PAGE_SIZE - 1))
#define BITS_TO_LONGS(n) 1
#define VM_SPECIAL 1
#define PG_swapbacked 0
#define PG_referenced 1
#define PG_workingset 2
#define PG_active 4
#define PG_reclaim 5
#define LRU_REFS_MASK BIT(3)
#define LRU_REFS_FLAGS (BIT(PG_referenced)|BIT(PG_workingset))
#define LRU_GEN_PGOFF 8
#define LRU_GEN_MASK (7UL<<LRU_GEN_PGOFF)
#define LRU_ACTIVE 1
#define LRU_INACTIVE_FILE 2
enum lru_list { LRU_IA, LRU_AA, LRU_IF=2, LRU_AF };
enum lru_gen_update_reason { LRU_GEN_UPDATE_ADD, LRU_GEN_UPDATE_DEL,
    LRU_GEN_UPDATE_INC, LRU_GEN_UPDATE_PTE, LRU_GEN_UPDATE_RMAP,
    NR_LRU_GEN_UPDATE_REASONS };
struct pglist_data { int node_id, lru_lock; unsigned long node_start_pfn; };
struct lru_gen_struct { unsigned long max_seq, min_seq[2]; bool enabled;
    unsigned long nr_pages[4][2][1]; int lists[4][2][1]; };
struct mem_cgroup;
struct lruvec { struct lru_gen_struct lrugen; struct pglist_data *pgdat;
    struct mem_cgroup *memcg; long compat[4]; };
struct mem_cgroup { struct lruvec *lruvec; int id; };
struct page { unsigned long flags; struct mem_cgroup *memcg; bool lru; };
enum { MM_PTE_TOTAL, MM_PTE_OLD, MM_PTE_YOUNG, NR_MM_STATS };
struct lru_gen_mm_walk { struct lruvec *lruvec; unsigned long max_seq;
    int batched; int nr_pages[4][2][1]; unsigned long mm_stats[NR_MM_STATS]; };
struct reclaim_state { struct lru_gen_mm_walk *mm_walk; };
struct task { struct reclaim_state *reclaim_state; };
static struct task task;
#define current (&task)
typedef struct { unsigned long pfn; bool young; } pte_t;
typedef struct { pte_t *ptes; } pmd_t;
typedef unsigned long spinlock_t;
struct mm_struct { int unused; };
struct vma { unsigned long vm_start, vm_end, vm_flags; };
struct mm_walk { struct mm_struct *mm; struct vma *vma; void *private; };
struct page_vma_mapped_walk {
    struct page *page; int *ptl; struct vma *vma;
    unsigned long address; pte_t *pte; void *pmd;
};
static struct page pages[33];
static unsigned activations;
static long lru_gen_rmap_owner_mismatch;
static long lru_gen_size_underflow;
static long lru_gen_size_underflow_pages;
static long lru_gen_batch_underflow;
static long lru_gen_batch_underflow_pages;
static long lru_gen_underflow_delete;
static long lru_gen_underflow_promote;
static long lru_gen_underflow_memcg_mismatch;
static long lru_gen_underflow_type_mismatch;
static long lru_gen_underflow_last_lruvec_memcg;
static long lru_gen_underflow_last_page_memcg;
static long lru_gen_underflow_last_old_gen;
static long lru_gen_underflow_last_new_gen;
static long lru_gen_underflow_last_type;
static long lru_gen_underflow_last_zone;
static long lru_gen_underflow_reason[NR_LRU_GEN_UPDATE_REASONS];
static long lru_gen_underflow_first_reason=-1;
static bool lru_locked;
#define atomic_long_inc(p) (++*(p))
#define atomic_long_add(v,p) (*(p)+=(v))
#define atomic_long_set(p,v) (*(p)=(v))
#define atomic_long_cmpxchg(p,o,n) ((*(p)==(o))?(*(p)=(n),(o)):*(p))
static void lru_gen_report_first_underflow(struct lruvec *l, struct page *p,
    long old_size, int old_gen, int new_gen, int type, int zone,
    enum lru_gen_update_reason reason)
{ (void)l; (void)p; (void)old_size; (void)old_gen; (void)new_gen;
  (void)type; (void)zone; (void)reason; }
#define lockdep_assert_held(p) ((void)(p))
#define spin_is_contended(p) false
#define spin_trylock(p) true
#define spin_lock_irq(p) ((void)(p),lru_locked=true)
#define spin_unlock_irq(p) ((void)(p),lru_locked=false)
#define spin_unlock(p) ((void)(p))
#define VM_BUG_ON(x) assert(!(x))
#define VM_BUG_ON_PAGE(x,p) assert(!(x))
#define WARN_ON_ONCE(x) (x)
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
#define rcu_read_lock_held() true
#define arch_enter_lazy_mmu_mode() ((void)0)
#define arch_leave_lazy_mmu_mode() ((void)0)
#define mem_cgroup_disabled() false
#define mem_cgroup_id(m) ((m)->id)
#define mem_cgroup_trylock_pages(m) true
#define mem_cgroup_unlock_pages() ((void)0)
#define page_memcg(p) ((p)->memcg)
#define page_memcg_rcu(p) ((p)->memcg)
#define page_pgdat(p) ((p)->memcg->lruvec->pgdat)
#define page_to_nid(p) (page_pgdat(p)->node_id)
#define lruvec_pgdat(l) ((l)->pgdat)
#define lruvec_memcg(l) ((l)->memcg)
#define mem_cgroup_lruvec(n,m) ((void)(n),(m)->lruvec)
#define pgdat_end_pfn(n) 34UL
#define PageLRU(p) ((p)->lru)
#define PageActive(p) (!!((p)->flags & BIT(PG_active)))
#define SetPageActive(p) ((p)->flags |= BIT(PG_active))
#define PageUnevictable(p) false
#define PageReclaim(p) (!!((p)->flags & BIT(PG_reclaim)))
#define PageWriteback(p) false
#define list_add(p,h) ((void)(p),(void)(h))
#define list_add_tail(p,h) list_add(p,h)
#define list_del(p) ((void)(p))
#define trace_mm_lru_activate(p) ((void)(p))
#define PGACTIVATE 0
#define __count_vm_event(e) ((void)(e))
#define update_page_reclaim_stat(l,f,n) ((void)(l),(void)(f),(void)(n))
#define PageDirty(p) false
#define PageAnon(p) true
#define PageSwapBacked(p) ((p)->flags & BIT(PG_swapbacked))
#define PageSwapCache(p) false
#define page_is_file_cache(p) (!PageSwapBacked(p))
#define set_page_dirty(p) ((void)(p))
#define SetPageReferenced(p) ((p)->flags |= BIT(PG_referenced))
#define pte_pfn(p) ((p).pfn)
#define pte_present(p) ((p).pfn != 0)
#define pte_young(p) ((p).young)
#define pte_dirty(p) false
#define pte_devmap(p) false
#define pte_special(p) false
#define is_zero_pfn(p) ((p)==0)
#define pfn_valid(p) ((p)>0 && (p)<34)
#define pfn_to_page(p) (&pages[(p)-1])
#define pte_page(p) pfn_to_page((p).pfn)
#define compound_head(p) (p)
#define page_zonenum(p) 0
#define hpage_nr_pages(p) 1
#define DECLARE_BITMAP(name,n) unsigned long name[BITS_TO_LONGS(n)]
#define __set_bit(i,b) (*(b) |= BIT(i))
#define bitmap_weight(b,n) __builtin_popcountl(*(b))
#define bitmap_empty(b,n) (*(b)==0)
#define for_each_set_bit(i,b,n) for ((i)=0;(i)<(n);(i)++) if (*(b)&BIT(i))
#define for_each_gen_type_zone(g,t,z) for(g=0;g<4;g++) for(t=0;t<2;t++) for(z=0;z<1;z++)
static bool ptep_clear_young_notify(struct vma *v, unsigned long a, pte_t *p)
{ (void)v; (void)a; bool old=p->young; p->young=false; return old; }
static bool suitable_to_scan(int n,int y) { return y*2>=n; }
static bool get_next_vma(struct mm_walk *w,unsigned long m,unsigned long s,
                         unsigned long *start,unsigned long *end)
{ (void)w;(void)m;(void)s;(void)start;(void)end;return false; }
#define pmd_trans_huge(p) false
#define pmd_devmap(p) false
#define pte_lockptr(mm,pmd) (&pages[32].flags)
#define pte_offset_map(pmd,start) ((void)(start),(pmd)->ptes)
#define pte_unmap(p) ((void)(p))
static void update_bloom_filter(struct lruvec *l,unsigned long s,void *p)
{ (void)l; (void)s; (void)p; }
static unsigned long cas(unsigned long *p,unsigned long old,unsigned long next)
{ unsigned long v=*p; if(v==old) *p=next; return v; }
#define cmpxchg cas
static void __update_lru_size(struct lruvec *l,enum lru_list i,int z,int d)
{ (void)z; l->compat[i]+=d; }
static void page_clear_lru_refs(struct page *p) { p->flags &= ~(LRU_REFS_MASK|LRU_REFS_FLAGS); }
'''

inline = (ROOT / "include/linux/mm_inline.h").read_text()
vmscan = (ROOT / "mm/vmscan.c").read_text()
code = PREFIX
for name in ("page_flags_is_file_cache", "lru_gen_from_seq", "lru_gen_is_active", "lru_gen_update_size"):
    code += function(inline, name)
for name in ("page_lru_gen", "page_update_gen", "update_batch_size", "reset_batch_size"):
    code += function(vmscan, name)
code += function(vmscan, "walk_pte_range")
for name in ("lru_gen_add_page", "lru_gen_del_page"):
    code += function(inline, name)
code += r'''
static void del_page_from_lru_list(struct page *p, struct lruvec *l)
{ assert(lru_locked); assert(lru_gen_del_page(l,p,false)); }
static void add_page_to_lru_list(struct page *p, struct lruvec *l)
{ assert(lru_locked); assert(lru_gen_add_page(l,p,false)); }
'''
code += function((ROOT / "mm/swap.c").read_text(), "__activate_page")
code += r'''
static struct page *activation_queue[33];
static int activation_count;
static void activate_page(struct page *p) {
    /* SMP activate_page() takes a reference and defers __activate_page(). */
    assert(PageLRU(p));
    activation_queue[activation_count++]=p;
    activations++;
}
static void drain_activations(void) {
    lru_locked=true;
    for(int i=0;i<activation_count;i++) {
        struct page *p=activation_queue[i];
        struct lruvec *l=p->memcg->lruvec;
        __activate_page(p,l,NULL);
    }
    activation_count=0;
    lru_locked=false;
}
'''
code += function(vmscan, "lru_gen_look_around")
code += r'''
static void check(struct lruvec *l, struct mem_cgroup *owner, int n) {
    long actual[4]={0}, compat[4]={0};
    for(int i=0;i<n;i++) if(pages[i].memcg==owner) actual[page_lru_gen(&pages[i])]++;
    for(int g=0;g<4;g++) {
        if ((long)l->lrugen.nr_pages[g][0][0] != actual[g]) {
            fprintf(stderr,"wrong owner: gen %d has %ld pages, expected %ld\n",g,
                (long)l->lrugen.nr_pages[g][0][0],actual[g]);
            assert((long)l->lrugen.nr_pages[g][0][0] == actual[g]);
        }
        compat[lru_gen_is_active(l,g)] += actual[g];
    }
    for(int i=0;i<4;i++) assert(l->compat[i]==compat[i]);
}
int main(void) {
    for(int foreign=0;foreign<2;foreign++) for(int n=1;n<=32;n++)
    for(unsigned seq=3;seq<11;seq++) {
        struct pglist_data node={.node_start_pfn=1};
        struct lruvec original={.pgdat=&node}, charged={.pgdat=&node};
        struct mem_cgroup root={&original}, app={&charged};
        struct lru_gen_mm_walk batch={.lruvec=&original};
        struct reclaim_state state={&batch};
        struct vma vma={.vm_end=n*PAGE_SIZE};
        struct page isolated={.flags=BIT(PG_swapbacked),.memcg=foreign?&app:&root};
        pte_t ptes[32]={0};
        struct lruvec *owner=isolated.memcg->lruvec;
        struct page_vma_mapped_walk pvmw={.page=&isolated,.ptl=&node.lru_lock,
            .vma=&vma,.pte=ptes};
        original.lrugen.max_seq=seq;
        charged.lrugen.max_seq=seq+1;
        original.lrugen.enabled=charged.lrugen.enabled=true;
        original.memcg=&root;
        charged.memcg=&app;
        task.reclaim_state=&state;
        activations=0;
        activation_count=0;
        lru_gen_rmap_owner_mismatch=0;
        memset(pages,0,sizeof(pages));
        int old=lru_gen_from_seq(owner->lrugen.max_seq-2);
        for(int i=0;i<n;i++) {
            pages[i].memcg=isolated.memcg;
            pages[i].lru=true;
            pages[i].flags=BIT(PG_swapbacked)|((old+1UL)<<LRU_GEN_PGOFF);
            owner->lrugen.nr_pages[old][0][0]++;
            owner->compat[0]++;
            ptes[i]=(pte_t){.pfn=i+1,.young=true};
        }
        lru_gen_look_around(&pvmw);
        assert(activation_count==(n<PAGEVEC_SIZE?n:0));
        drain_activations();
        /* No rmap generation delta may survive the PTE-locked operation. */
        reset_batch_size(&original,&batch);
        check(&original,&root,n);
        check(&charged,&app,n);
        for(int i=0;i<n;i++) assert(page_lru_gen(&pages[i])==lru_gen_from_seq(owner->lrugen.max_seq));
        assert(!batch.batched);
        for(int g=0;g<4;g++) assert(!batch.nr_pages[g][0][0]);
        assert(lru_gen_rmap_owner_mismatch==foreign);
    }
    for(int n=1;n<=32;n++) for(unsigned seq=3;seq<11;seq++) {
        struct pglist_data node={.node_start_pfn=1};
        struct lruvec lru={.pgdat=&node};
        struct mem_cgroup owner={&lru};
        struct lru_gen_mm_walk priv={.lruvec=&lru,.max_seq=seq};
        struct mm_struct mm={0};
        struct vma vma={.vm_end=n*PAGE_SIZE};
        struct mm_walk walk={.mm=&mm,.vma=&vma,.private=&priv};
        pte_t ptes[32]={0};
        pmd_t pmd={.ptes=ptes};
        int old=lru_gen_from_seq(seq-2);
        lru.memcg=&owner;
        lru.lrugen.max_seq=seq;
        memset(pages,0,sizeof(pages));
        for(int i=0;i<n;i++) {
            pages[i].memcg=&owner;
            pages[i].lru=true;
            pages[i].flags=BIT(PG_swapbacked)|((old+1UL)<<LRU_GEN_PGOFF);
            lru.lrugen.nr_pages[old][0][0]++;
            lru.compat[0]++;
            ptes[i]=(pte_t){.pfn=i+1,.young=true};
        }
        assert(walk_pte_range(&pmd,0,n*PAGE_SIZE,&walk));
        check(&lru,&owner,n);
        for(int i=0;i<n;i++)
            assert(page_lru_gen(&pages[i])==lru_gen_from_seq(seq));
        assert(!priv.batched);
        assert(!lru_locked);
    }
    for(unsigned seq=3;seq<11;seq++) {
        struct pglist_data node={0};
        struct lruvec before={.pgdat=&node}, after={.pgdat=&node};
        struct mem_cgroup source={&before}, target={&after};
        before.memcg=&source; after.memcg=&target;
        before.lrugen.enabled=after.lrugen.enabled=true;
        before.lrugen.max_seq=seq; after.lrugen.max_seq=seq+1;
        memset(pages,0,sizeof(pages));
        pages[0].memcg=&source;
        pages[0].lru=true;
        pages[0].flags=BIT(PG_swapbacked);
        lru_locked=true;
        add_page_to_lru_list(&pages[0],&before);
        lru_locked=false;
        activate_page(&pages[0]);
        /* A queued reference survives isolation and a charge migration. */
        lru_locked=true;
        del_page_from_lru_list(&pages[0],&before);
        pages[0].lru=false;
        pages[0].memcg=&target;
        add_page_to_lru_list(&pages[0],&after);
        pages[0].lru=true;
        lru_locked=false;
        drain_activations();
        check(&before,&source,1);
        check(&after,&target,1);
        assert(page_lru_gen(&pages[0])==lru_gen_from_seq(seq+1));
    }
    assert(!lru_gen_size_underflow);
    puts("PASS: 512 deferred-rmap, 256 PTE, and 8 queued-charge-migration accounting cases");
}
'''
with tempfile.TemporaryDirectory(prefix="e404-mglru-accounting-") as tmp:
    src = Path(tmp) / "accounting.c"
    exe = Path(tmp) / "accounting"
    src.write_text(code)
    subprocess.run([os.environ.get("CC", "clang"), "-std=gnu11", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

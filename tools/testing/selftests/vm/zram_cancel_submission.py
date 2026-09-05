#!/usr/bin/env python3
"""Exercise actual ZRAM submission loops; cancel without abandoning live I/O."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).resolve().parents[4]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
typedef uint32_t u32;
typedef uint64_t u64;
struct page { int unused; };
struct list_head { bool present; int index; };
struct zram_pp_slot { u32 index; struct list_head entry; };
struct zram_pp_ctl { struct zram_pp_slot slots[4]; };
struct zram { int wb_batch_size; bool wb_limit_enable; int bd_wb_limit; bool compressed_wb; void *bdev; };
struct zram_prefetch_ctl { int num_inflight; uint64_t prefetched_pages; int done_wait; };
struct bio { void *bi_private, *bi_end_io; int bi_opf; struct { unsigned long bi_sector; } bi_iter; };
struct zram_wb_req { struct page *page; unsigned long blk_idx; struct zram_pp_slot *pps; struct bio bio; int bio_vec; };
struct zram_wb_ctl { int num_inflight, done_wait; struct list_head done_reqs; struct zram_wb_req req; };
static bool cancelled;
static int scenario, submits, completions, releases, live_pages;
static struct page dummy;
#define current NULL
#define fatal_signal_pending(t) (cancelled)
#define max_t(t,a,b) ((t)((a)>(b)?(a):(b)))
#define atomic_set(p,v) (*(p)=(v))
#define atomic64_set(p,v) (*(p)=(v))
#define atomic_read(p) (*(p))
#define atomic64_read(p) (*(p))
#define init_waitqueue_head(p) (*(p)=17)
#define ZRAM_PP_SLOT 1
#define ZRAM_WB 2
#define GFP_NOIO 0
#define __GFP_NOWARN 0
#define INVALID_BDEV_BLOCK (~0UL)
#define REQ_OP_WRITE 0
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define cond_resched() ((void)0)
static void simulated_completion(int *queue) {
    if (*queue == 17) {
        struct zram_prefetch_ctl *c = (void *)((char *)queue - offsetof(struct zram_prefetch_ctl,done_wait));
        assert(c->num_inflight > 0); c->num_inflight--; c->prefetched_pages++; live_pages--; completions++;
    } else {
        struct zram_wb_ctl *c = (void *)((char *)queue - offsetof(struct zram_wb_ctl,done_wait));
        assert(c->num_inflight > 0); c->done_reqs.present=true;
    }
}
#define wait_event(q,c) do { if (!(c)) simulated_completion(&(q)); assert(c); } while (0)
#define wait_event_killable(q,c) ({ int r=0; if (!(c)) { if (scenario==2) cancelled=true; if (cancelled) r=-EINTR; else simulated_completion(&(q)); } r; })
static struct zram_pp_slot *select_pp_slot(struct zram_pp_ctl *c) {
    for(int i=0;i<4;i++) if(c->slots[i].entry.present) return &c->slots[i];
    return NULL;
}
static void release_pp_slot(struct zram *z, struct zram_pp_slot *p) { (void)z; p->entry.present=false; releases++; }
static void zram_slot_lock(struct zram *z,u32 i) { (void)z;(void)i; }
#define zram_slot_unlock zram_slot_lock
static bool zram_test_flag(struct zram *z,u32 i,int f) { (void)z;(void)i;(void)f; return true; }
static unsigned long zram_get_handle(struct zram *z,u32 i) { (void)z;return i; }
static struct page *alloc_page(int flags) { (void)flags;live_pages++;return &dummy; }
static void __free_page(struct page *p) { (void)p;live_pages--; }
static int zram_prefetch_from_bdev(struct zram *z,struct page *p,u32 i,unsigned long b,struct zram_prefetch_ctl *c) {
    (void)z;(void)p;(void)i;(void)b;c->num_inflight++;submits++;if(scenario==1)cancelled=true;return 0;
}
static bool list_empty(struct list_head *h) { return !h->present; }
static void list_del_init(struct list_head *h) { h->present=false; }
static struct zram_wb_req *zram_select_idle_req(struct zram_wb_ctl *c) { return c->num_inflight ? NULL : &c->req; }
static int zram_complete_done_reqs(struct zram *z,struct zram_wb_ctl *c) {
    assert(c->num_inflight==1 && c->done_reqs.present);c->num_inflight--;c->done_reqs.present=false;
    release_pp_slot(z,c->req.pps);completions++;return 0;
}
static unsigned long alloc_block_bdev(struct zram *z) { (void)z;return 1; }
static void free_block_bdev(struct zram *z,unsigned long b) { (void)z;(void)b; }
static int zram_prefetch_cache_reuse(struct zram *z,u32 i) { (void)z;(void)i;return 1; }
static int zram_read_from_zspool_raw(struct zram *z,struct page *p,u32 i) { (void)z;(void)p;(void)i;return 0; }
#define zram_read_from_zspool zram_read_from_zspool_raw
static void bio_init(struct bio *b,int *v,int n) { (void)b;(void)v;(void)n; }
static void bio_set_dev(struct bio *b,void *d) { (void)b;(void)d; }
static void __bio_add_page(struct bio *b,struct page *p,int n,int o) { (void)b;(void)p;(void)n;(void)o; }
#define zram_writeback_endio NULL
static void zram_submit_wb_request(struct zram *z,struct zram_wb_ctl *c,struct zram_wb_req *r) {
    (void)z;(void)r;c->num_inflight++;submits++;if(scenario==1)cancelled=true;
}
static void release_wb_req(struct zram_wb_req *r) { (void)r; }
'''
suffix = r'''
int main(void) {
    for (int kind=0;kind<2;kind++) for(scenario=0;scenario<4;scenario++) {
        struct zram z={.wb_batch_size=1};struct zram_pp_ctl pp={0};
        struct zram_wb_ctl wb={.done_wait=18};
        for(int i=0;i<4;i++) { pp.slots[i].index=i;pp.slots[i].entry.present=true; }
        cancelled=scenario==3;submits=completions=releases=live_pages=0;
        u64 prefetched=0;
        int result=kind ? zram_writeback_slots(&z,&pp,&wb) : zram_prefetch_slots(&z,&pp,&prefetched);
        assert(result==(scenario ? -EINTR : 0));
        int expected=scenario==0 ? 4 : scenario==3 ? 0 : 1;
        assert(submits==expected && completions==submits && releases==submits);
        assert(wb.num_inflight==0 && live_pages==0);
        if(!kind) assert(prefetched==(u64)completions);
        int unsent=0;for(int i=0;i<4;i++) unsent+=pp.slots[i].entry.present;
        assert(unsent==4-submits); /* caller retains cleanup ownership */
    }
    puts("PASS: cancelled ZRAM work stops submission and drains all live I/O");
}
'''
def extract(text):
    pieces=[]
    for start,end in [('int zram_prefetch_slots(', '#define PAGE_WB_SIG'), ('int zram_writeback_slots(', 'static ssize_t writeback_store(')]:
        a=text.index(start);b=text.index(end,a);pieces.append(text[a:b])
    return '\n'.join(pieces)
cases=[('current',(source/'drivers/block/zram/zram_drv.c').read_text(),True)]
if len(sys.argv)>1:
    cases.append(('baseline',subprocess.check_output(['git','-C',str(source),'show',sys.argv[1]+':drivers/block/zram/zram_drv.c'],text=True),False))
for label,text,expected in cases:
    with tempfile.TemporaryDirectory() as d:
        c=pathlib.Path(d)/'test.c';exe=c.with_suffix('');c.write_text(prefix+extract(text)+suffix)
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-sign-compare','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True)
        assert (r.returncode==0)==expected,(label,r.stdout,r.stderr)
        print(label+': '+(r.stdout.strip() if expected else 'expected cancellation assertion failure'))

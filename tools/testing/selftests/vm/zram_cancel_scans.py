#!/usr/bin/env python3
"""Fault-inject cancellation into actual ZRAM sysfs scans and their cleanup paths."""
import pathlib
import re
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).resolve().parents[4]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t ktime_t;
#define PAGE_SHIFT 12
#define SLOTS 1024
#define CONFIG_ZRAM_MEMORY_TRACKING 1
#define IS_ENABLED(x) (x)
#define NSEC_PER_SEC 1000000000ULL
#define ZRAM_MAX_COMPS 3
#define ZRAM_PRIMARY_COMP 0
#define ZRAM_SECONDARY_COMP 1
#define GFP_KERNEL 0
#define current NULL
#define min(a,b) ((a)<(b)?(a):(b))
#define atomic64_read(p) (*(p))
#define atomic_set(p,v) (*(p)=(v))
#define atomic_xchg(p,v) exchange(p,v)
#define PAGE_WB_SIG "page_index="
#define PAGE_WRITEBACK 0
#define HUGE_WRITEBACK 1
#define IDLE_WRITEBACK 2
#define INCOMPRESSIBLE_WRITEBACK 4
#define RECOMPRESS_IDLE 1
#define RECOMPRESS_HUGE 2
#define ZRAM_WB 1
#define ZRAM_SAME 2
#define ZRAM_IDLE 4
#define ZRAM_HUGE 8
#define ZRAM_INCOMPRESSIBLE 16
#define ZRAM_PP_SLOT 32
static const int huge_class_size = 4096;
struct page { int unused; };
struct device_attribute { int unused; };
struct zram_pp_slot { u32 index; bool live; };
struct zram_pp_ctl { struct zram_pp_slot slots[SLOTS]; };
struct zram_wb_ctl { int unused; };
struct zram {
    uint64_t disksize;
    int init_lock, pp_in_progress, num_active_comps;
    void *backing_dev, *mem_pool;
    char *comp_algs[ZRAM_MAX_COMPS];
    struct { ktime_t ac_time; unsigned flags; } table[SLOTS];
    struct { uint64_t bd_writes, compr_data_size; } stats;
    int last_writeback_action, last_recompress_action;
};
struct device { struct zram *zram; };
static int scenario, locks, visits, yields, queued, pages, controls;
static int processed, writebacks, finishes, finish_status, compactions;
static bool cancelled, empty_slots, boosted;
static bool mem_boost_active(void) { return boosted; }
static int exchange(int *p,int v) { int old=*p;*p=v;return old; }
static void down_read(int *p) { assert(*p==0);++*p; }
static void up_read(int *p) { assert(*p==1);--*p; }
static bool init_done(struct zram *z) { return z->disksize!=0; }
static struct zram *dev_to_zram(struct device *d) { return d->zram; }
static void cond_resched(void) {
    assert(locks==0); yields++; if(scenario==3 && yields==2) cancelled=true;
    if(scenario==5 && yields==2) boosted=true;
}
static bool fatal_signal_pending(void *p) { return cancelled; }
static void zram_slot_lock(struct zram *z,u32 i) {
    assert(locks==0 && i<SLOTS);locks++;visits++;
}
static void zram_slot_unlock(struct zram *z,u32 i) {
    assert(locks==1);locks--;if(scenario==2 && visits==73) cancelled=true;
}
static bool zram_allocated(struct zram *z,u32 i) { return !empty_slots; }
static bool zram_test_flag(struct zram *z,u32 i,int f) { return z->table[i].flags & f; }
static void zram_set_flag(struct zram *z,u32 i,int f) { z->table[i].flags |= f; }
static void zram_clear_flag(struct zram *z,u32 i,int f) { z->table[i].flags &= ~f; }
static bool ktime_after(ktime_t a,ktime_t b) { return a>b; }
static ktime_t ktime_sub(ktime_t a,ktime_t b) { return a-b; }
static ktime_t ktime_get_boottime(void) { return 1000000000000ULL; }
static ktime_t ns_to_ktime(uint64_t a) { return a; }
static bool sysfs_streq(const char *a,const char *b) { return strcmp(a,b)==0; }
static int kstrtoull(const char *s,int base,uint64_t *p) { *p=strtoull(s,NULL,base);return 0; }
static int kstrtouint(const char *s,int base,u32 *p) { *p=strtoul(s,NULL,base);return 0; }
static int kstrtol(const char *s,int base,void *p) { *(unsigned long *)p=strtoul(s,NULL,base);return 0; }
static char *skip_spaces(const char *s) { while(isspace(*s))s++;return (char *)s; }
static char *next_arg(char *s,char **p,char **v) { abort(); }
static u32 zram_get_priority(struct zram *z,u32 i) { return 0; }
static bool zram_prefetch_cache_exists(struct zram *z,u32 i) { return false; }
static struct page *alloc_page(int flags) { pages++;return calloc(1,sizeof(struct page)); }
static void __free_page(struct page *p) { pages--;free(p); }
static struct zram_pp_ctl *zram_pp_ctl_alloc(void) { controls++;return calloc(1,sizeof(struct zram_pp_ctl)); }
static struct zram_wb_ctl *zram_wb_ctl_alloc(struct zram *z) { controls++;return calloc(1,sizeof(struct zram_wb_ctl)); }
static void zram_wb_ctl_free(struct zram_wb_ctl *c) { if(c) { controls--;free(c); } }
static bool place_pp_slot(struct zram *z,struct zram_pp_ctl *c,u32 i) {
    assert(locks==1 && !c->slots[i].live);
    c->slots[i]=(struct zram_pp_slot){.index=i,.live=true};queued++;
    zram_set_flag(z,i,ZRAM_PP_SLOT);return true;
}
static struct zram_pp_slot *select_pp_slot(struct zram_pp_ctl *c) {
    for(int i=0;i<SLOTS;i++) if(c->slots[i].live)return &c->slots[i];return NULL;
}
static void release_pp_slot(struct zram *z,struct zram_pp_slot *p) {
    assert(locks==0 && p->live);p->live=false;queued--;zram_clear_flag(z,p->index,ZRAM_PP_SLOT);
}
static void zram_pp_ctl_free(struct zram *z,struct zram_pp_ctl *c) {
    if(!c)return;
    struct zram_pp_slot *p;while((p=select_pp_slot(c)))release_pp_slot(z,p);
    controls--;free(c);
}
static void zram_action_begin(struct zram *z,int *a,int kind,int mode,uint64_t pages,uint64_t bytes) { *a=1; }
static void zram_action_finish(struct zram *z,int *a,int status,uint64_t bytes) {
    assert(*a==1);finishes++;finish_status=status;
}
static int zram_writeback_slots(struct zram *z,struct zram_pp_ctl *p,struct zram_wb_ctl *w) {
    assert(!cancelled);writebacks++;return 0;
}
static void zs_compact(void *p) { assert(!cancelled);compactions++; }
static int recompress_slot(struct zram *z,u32 i,struct page *p,u64 *n,u32 t,u32 prio,u32 max) {
    assert(locks==1);processed++;--*n;if(scenario==4)cancelled=true;return 0;
}
'''
suffix = r'''
int main(int argc,char **argv) {
    assert(argc==4);int kind=atoi(argv[1]);scenario=atoi(argv[2]);empty_slots=atoi(argv[3]);
    struct zram z={.disksize=(uint64_t)SLOTS<<PAGE_SHIFT,.num_active_comps=2,.backing_dev=&z};
    struct device d={.zram=&z};
    for(int i=0;i<SLOTS;i++)z.table[i].flags=ZRAM_HUGE;
    cancelled=scenario==1;boosted=scenario==6;
    ssize_t result=kind==0 ? idle_store(&d,NULL,"all",3) :
        kind==1 ? writeback_store(&d,NULL,"huge",4) : recompress_store(&d,NULL," ",1);
    int error=scenario>=5 ? -EBUSY : scenario ? -EINTR : 0;
    assert(result==(error ? error : kind==0 ? 3 : kind==1 ? 4 : 1));
    assert(locks==0 && z.init_lock==0 && z.pp_in_progress==0);
    assert(pages==0 && controls==0 && queued==0);
    for(int i=0;i<SLOTS;i++)assert(!(z.table[i].flags & ZRAM_PP_SLOT));
    if(kind && scenario!=6)assert(finishes==1 && finish_status==error);
    if(scenario==6)assert(finishes==0 && visits==0 && writebacks==0);
    if(scenario==5)assert(visits==256 && writebacks==0);
    if(scenario==1)assert(visits==0 && processed==0 && writebacks==0);
    if(scenario==2)assert(visits==73 && processed==0 && writebacks==0);
    if(scenario==3)assert(visits==256 && processed==0 && writebacks==0);
    if(scenario==4)assert(processed==1);
    if(!scenario) {
        if(EXPECT_SCAN_RESCHED)assert(yields>=4);
        if(kind==1)assert(writebacks==1);
        if(kind==2)assert(processed==(empty_slots ? 0 : SLOTS));
    }
    puts("PASS");
}
'''

def extract(text, name):
    match = re.search(r'^(?:static )?(?:void|int|ssize_t) '+name+r'\(', text, re.M)
    assert match, name
    start = match.start()
    brace = text.index('{', match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

cases=[('current',(source/'drivers/block/zram/zram_drv.c').read_text())]
if len(sys.argv)>1:
    cases.append(('baseline',subprocess.check_output(['git','-C',str(source),'show',sys.argv[1]+':drivers/block/zram/zram_drv.c'],text=True)))
for label,text in cases:
    with tempfile.TemporaryDirectory() as d:
        c=pathlib.Path(d)/'test.c';exe=c.with_suffix('')
        c.write_text(f"#define EXPECT_SCAN_RESCHED {int(label == 'current')}\n"+prefix+'\n'.join(extract(text,n) for n in (
            'mark_idle','idle_store','scan_slots_for_writeback','writeback_store',
            'scan_slots_for_recompress','recompress_store'))+suffix)
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',
            '-Wno-sign-compare','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        passed=failed=0
        for kind in range(3):
            for scenario in (list(range(5 if kind==2 else 4)) + ([5,6] if kind==1 else [])):
                for empty in (0,1):
                    if scenario==4 and empty:continue
                    r=subprocess.run([str(exe),str(kind),str(scenario),str(empty)],capture_output=True,text=True,timeout=5)
                    # K93 cancels on signals but lacks launch deferral.
                    expected=label=='current' or scenario<5
                    assert (r.returncode==0)==expected,(label,kind,scenario,empty,r.stdout,r.stderr)
                    if expected:passed+=1
                    else:failed+=1
        print(f'{label}: {passed} passed, {failed} expected regression failures')

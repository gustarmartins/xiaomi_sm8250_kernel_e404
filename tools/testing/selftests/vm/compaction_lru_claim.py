#!/usr/bin/env python3
"""Schedule reclaim at the actual compaction lock boundary; K88 must fail."""
import pathlib, subprocess, sys, tempfile
source=pathlib.Path(__file__).resolve().parents[4]
def section(text):
    start=text.index('\t\t/*\n\t\t * Be careful not to clear PageLRU')
    end=text.index('\n\t\tinc_node_page_state(page,', start)
    return text[start:end]
prefix=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
struct page { int refs; bool lru, compound; int owner; };
struct zone { void *zone_pgdat; };
struct lruvec { int unused; };
static struct lruvec lv;
static bool lock_held, race_reclaim, skip;
static int compactor_claimed;
#define spin_unlock_irqrestore(l,f) (lock_held=false)
#define put_page(p) ((p)->refs--)
#define PageLRU(p) ((p)->lru)
#define PageCompound(p) ((p)->compound)
#define SetPageLRU(p) ((p)->lru=true)
#define VM_BUG_ON_PAGE(x,p) assert(!(x))
#define unlikely(x) (x)
#define compound_order(p) 0
#define zone_lru_lock(z) ((void *)0)
#define mem_cgroup_page_lruvec(p,n) (&lv)
static bool get_page_unless_zero(struct page *p) { if (!p->refs) return false; p->refs++; return true; }
static int __isolate_lru_page_prepare(struct page *p,int mode) { return p->lru ? 0:-1; }
static bool TestClearPageLRU(struct page *p) { bool old=p->lru; p->lru=false; return old; }
static struct page *selected;
static bool compact_lock_irqsave(void *l,unsigned long *f,void *cc) {
    if (race_reclaim) {
        /* Actual MGLRU isolate_page() uses list membership and a refcount
         * pin under lru_lock, then ClearPageLRU and lru_gen_del_page. */
        selected->refs++; selected->lru=false; selected->owner=2;
    }
    lock_held=true; return true;
}
static bool test_and_set_skip(void *cc,struct page *p,unsigned long pfn) { return skip; }
static void del_page_from_lru_list(struct page *p,struct lruvec *l) {
    assert(lock_held); assert(p->owner==0);
    p->owner=1; compactor_claimed++;
}
static int claim(struct page *page) {
    struct zone z={0}, *zone=&z; struct lruvec *lruvec;
    bool locked=false, skip_updated=false;
    unsigned long flags=0,low_pfn=0; void *cc=0; int isolate_mode=0;
    selected=page;
"""
suffix=r"""
    lock_held=false; return 1;
ABORT_CLEANUP
    return 0;
isolate_fail_put:
    lock_held=false; page->refs--;
isolate_fail:
    return 0;
}
int main(void) {
    for(int race=0;race<2;race++) {
        struct page p={1,true,false,0}; race_reclaim=race;
        compactor_claimed=0; skip=false;
        int result=claim(&p);
        assert(result==!race && compactor_claimed==!race);
        assert(p.owner==(race?2:1));
        assert(p.refs==2);
    }
    for(int race=0;race<2;race++) {
        struct page p={1,true,false,0}; race_reclaim=race; skip=true;
        assert(claim(&p)==0);
        assert(p.lru==!race && p.owner==(race?2:0));
        assert(p.refs==(race?2:1));
    }
    puts("PASS: compaction cannot steal a page isolated while waiting for LRU lock");
}
"""
cases=[('current',(source/'mm/compaction.c').read_text(),True)]
if len(sys.argv)>1:
    cases.append(('baseline',subprocess.check_output(['git','-C',str(source),'show',sys.argv[1]+':mm/compaction.c'],text=True),False))
for label,text,should_pass in cases:
    with tempfile.TemporaryDirectory() as tmp:
        c=pathlib.Path(tmp)/'test.c';binary=c.with_suffix('')
        abort_start=text.index('\nisolate_abort:',text.index('isolate_migratepages_block('))
        abort_end=text.index('\n\t/*',abort_start)
        c.write_text(prefix+section(text)+suffix.replace('ABORT_CLEANUP',text[abort_start:abort_end]))
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-but-set-variable','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode==0)==should_pass,(label,result.stderr)
        print(label+': '+('PASS' if should_pass else 'expected double-isolation assertion failure'))

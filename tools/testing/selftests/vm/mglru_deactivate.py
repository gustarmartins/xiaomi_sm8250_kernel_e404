#!/usr/bin/env python3
"""Exercise actual MADV_COLD queue and LRU callback gates, including K88 failure."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).resolve().parents[4]

def extract(text, name):
    start = text.index('void ' + name + '(')
    start = text.rindex('\n', 0, start) + 1
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

prefix = r'''
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
struct page { bool lru, active, unevictable, referenced; };
struct lruvec { int unused; };
struct pagevec { int unused; };
static bool mglru;
static int removed, added, queued, events;
static struct pagevec lru_deactivate_pvecs;
static struct lruvec lruvec;
#define PageLRU(p) ((p)->lru)
#define PageActive(p) ((p)->active)
#define PageUnevictable(p) ((p)->unevictable)
#define PageCompound(p) false
#define ClearPageActive(p) ((p)->active=false)
#define ClearPageReferenced(p) ((p)->referenced=false)
#define lru_gen_enabled() mglru
#define page_is_file_cache(p) 0
#define hpage_nr_pages(p) 1
#define PGDEACTIVATE 0
#define __count_vm_events(e,n) (events+=(n))
#define get_cpu_var(v) (v)
#define put_cpu_var(v) ((void)0)
#define get_page(p) ((void)0)
#define update_page_reclaim_stat(l,f,r) ((void)0)
#define del_page_from_lru_list(p,l) (removed++)
#define add_page_to_lru_list(p,l) (added++)
static bool pagevec_add(struct pagevec *v, struct page *p) {
    queued++; return true;
}
static void pagevec_lru_move_fn(struct pagevec *v,
        void (*fn)(struct page *, struct lruvec *, void *), void *arg) {}
'''
main = r'''
int main(void) {
    for (int gen=0; gen<2; gen++)
    for (int active=0; active<2; active++)
    for (int on_lru=0; on_lru<2; on_lru++)
    for (int unevictable=0; unevictable<2; unevictable++) {
        struct page p={on_lru,active,unevictable,true};
        mglru=gen; removed=added=queued=events=0;
        bool should_deactivate=on_lru && !unevictable && (active || gen);
        deactivate_page(&p);
        assert(queued == should_deactivate);
        lru_deactivate_fn(&p,&lruvec,NULL);
        assert(removed == should_deactivate && added == should_deactivate);
        assert(events == should_deactivate);
        if (should_deactivate) assert(!p.active && !p.referenced);
    }
    /* Queue admission never replaces callback-time LRU/unevictable checks. */
    mglru=true; struct page p={true,false,false,true};
    removed=added=queued=events=0;
    deactivate_page(&p); assert(queued == 1);
    p.unevictable=true;
    lru_deactivate_fn(&p,&lruvec,NULL); assert(removed == 0);
    puts("PASS: MADV_COLD reaches MGLRU and rechecks page eligibility");
}
'''
cases=[('current',(source/'mm/swap.c').read_text(),True)]
if len(sys.argv)>1:
    cases.append(('baseline',subprocess.check_output(['git','-C',str(source),'show',sys.argv[1]+':mm/swap.c'],text=True),False))
for label, text, should_pass in cases:
    with tempfile.TemporaryDirectory() as tmp:
        c=pathlib.Path(tmp)/'test.c'; binary=c.with_suffix('')
        c.write_text(prefix+'\n'+extract(text,'lru_deactivate_fn')+'\n'+extract(text,'deactivate_page')+'\n'+main)
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        assert (result.returncode==0)==should_pass,(label,result.stderr)
        print(label+': '+('PASS' if should_pass else 'expected assertion failure'))

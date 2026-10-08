#!/usr/bin/env python3
"""Test the production bounded history container; not kernel lock qualification."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define WRITE_ONCE(x,v) ((x)=(v))
'''
code += (root / "mm/mglru_history.h").read_text()
code += r'''
static struct mglru_history h;
int main(void)
{
    struct mglru_history_event e = {0};
    mglru_history_freeze(&h, 17);
    assert(h.frozen && !h.matched && !h.count);
    mglru_history_push(&h, &e);
    assert(!h.count);
    memset(&h, 0, sizeof(h));
    for (unsigned i=0; i<2*MGLRU_HISTORY_SLOTS+123; i++) {
        e.pfn = i%3 ? 17 : 99;
        e.owner = i; e.charge = i+1; e.old_size = i*3;
        mglru_history_push(&h, &e);
    }
    u64 count=h.count;
    mglru_history_freeze(&h, 17);
    assert(h.matched==MGLRU_HISTORY_MATCHES);
    unsigned expected=0;
    for (u64 seq=count-MGLRU_HISTORY_SLOTS+1; seq<=count; seq++)
        expected += (seq-1)%3 != 0;
    assert(h.total_matches==expected);
    for(unsigned i=0;i<h.matched;i++) {
        struct mglru_history_event *r=&h.matches[i];
        assert(r->pfn==17 && r->owner==r->sequence-1);
        assert(r->charge==r->sequence && r->old_size==r->owner*3);
        assert(r->sequence>count-MGLRU_HISTORY_SLOTS);
        if(i) assert(r->sequence<h.matches[i-1].sequence);
    }
    e.pfn=88;
    mglru_history_push(&h,&e);
    mglru_history_freeze(&h,88);
    assert(h.count==count && h.total_matches==expected && h.matches[0].pfn==17);
    memset(&h,0,sizeof(h));
    e.pfn=17; mglru_history_push(&h,&e);
    e.pfn=99;
    for(unsigned i=0;i<MGLRU_HISTORY_SLOTS;i++) mglru_history_push(&h,&e);
    mglru_history_freeze(&h,17);
    assert(!h.matched && !h.total_matches); /* overwritten is not missing credit proof */
    printf("PASS: empty, rollover, match cap, coherent fields, frozen snapshot, overwritten history; bytes=%zu\n",sizeof(h));
}
'''
with tempfile.TemporaryDirectory(prefix="e404-history-") as directory:
    source, binary = Path(directory)/"test.c", Path(directory)/"test"
    source.write_text(code)
    subprocess.run([os.environ.get("CC", "clang"), "-std=gnu11", "-O1", "-g",
                    "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

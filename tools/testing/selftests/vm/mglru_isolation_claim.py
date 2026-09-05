#!/usr/bin/env python3
"""Exercise the actual MGLRU isolator with a pre-lock claim by SHM_UNLOCK."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).resolve().parents[4]
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
struct page { int refs; bool lru, mapped, dirty, anon, swapcache; int owner; };
struct lruvec { int deleted; };
struct scan_control { bool may_unmap, may_writepage; unsigned int gfp_mask; };
#define __GFP_IO 1
#define page_mapped(p) ((p)->mapped)
#define PageDirty(p) ((p)->dirty)
#define PageAnon(p) ((p)->anon)
#define PageSwapCache(p) ((p)->swapcache)
#define ClearPageLRU(p) ((p)->lru = false)
#define VM_BUG_ON_PAGE(x,p) assert(!(x))
static bool get_page_unless_zero(struct page *p) {
    if (!p->refs) return false;
    p->refs++;
    return true;
}
static void put_page(struct page *p) { p->refs--; }
static bool TestClearPageLRU(struct page *p) {
    bool old = p->lru;
    p->lru = false;
    return old;
}
static bool lru_gen_del_page(struct lruvec *lv, struct page *p, bool reclaim) {
    (void)reclaim;
    assert(p->owner == 0); /* another isolator still owns this list entry */
    p->owner = 1;
    lv->deleted++;
    return true;
}
"""
suffix = r"""
int main(void) {
    struct scan_control sc = {true, true, __GFP_IO};
    for (int claimed = 0; claimed < 2; claimed++) {
        struct lruvec lv = {0};
        struct page p = {.refs=1, .lru=!claimed, .owner=claimed ? 2 : 0};
        /* SHM_UNLOCK owns PG_lru but is waiting for the node LRU lock.
         * The page remains on the list being scanned by MGLRU. */
        assert(isolate_page(&lv, &p, &sc) == !claimed);
        assert(lv.deleted == !claimed);
        assert(p.refs == (claimed ? 1 : 2));
        assert(p.owner == (claimed ? 2 : 1));
        assert(!p.lru);
    }
    struct lruvec lv = {0};
    struct page dead = {.lru=true};
    assert(!isolate_page(&lv, &dead, &sc));
    assert(dead.lru && dead.refs == 0 && lv.deleted == 0);
    struct page mapped = {.refs=1, .lru=true, .mapped=true};
    sc.may_unmap = false;
    assert(!isolate_page(&lv, &mapped, &sc));
    assert(mapped.lru && mapped.refs == 1 && lv.deleted == 0);
    puts("PASS: MGLRU respects an existing isolation claim and balances its pin");
}
"""
cases = [("current", (source / "mm/vmscan.c").read_text(), True)]
if len(sys.argv) > 1:
    baseline = subprocess.check_output(
        ["git", "-C", str(source), "show", sys.argv[1] + ":mm/vmscan.c"], text=True)
    cases.append(("baseline", baseline, False))
for label, text, should_pass in cases:
    start = text.index("static bool isolate_page(struct lruvec")
    end = text.index("\nstatic int scan_pages(", start)
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp) / "test.c"
        binary = c.with_suffix("")
        c.write_text(prefix + text[start:end] + suffix)
        subprocess.run([
            "clang", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
            "-fsanitize=address,undefined", str(c), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert (result.returncode == 0) == should_pass, (label, result.stderr)
        print(label + ": " + (result.stdout.strip() if should_pass else
              "expected double-isolation assertion failure"))

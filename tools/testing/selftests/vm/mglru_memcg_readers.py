#!/usr/bin/env python3
"""Compile actual reader definitions, and verify the unpatched source fails."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).resolve().parents[4]
PREFIX = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#define CONFIG_LRU_GEN 1
#define MAX_NR_ZONES 2
#define MAX_NR_GENS 4
#define READ_ONCE(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define LRU_ACTIVE 1
#define LRU_FILE 2
enum lru_list { LRU_INACTIVE_ANON, LRU_ACTIVE_ANON, LRU_INACTIVE_FILE, LRU_ACTIVE_FILE, LRU_UNEVICTABLE, NR_LRU_LISTS };
struct lru_gen_struct { unsigned long max_seq; long nr_pages[4][2][2]; };
struct lruvec { struct lru_gen_struct lrugen; };
struct mem_cgroup_per_node { struct lruvec lruvec; unsigned long lru_zone_size[2][5]; };
'''

MAIN = r'''
int main(void) {
 struct mem_cgroup_per_node m={0}; struct lruvec *l=&m.lruvec;
 l->lrugen.max_seq=3;
 m.lru_zone_size[0][LRU_INACTIVE_ANON]=7;
 l->lrugen.nr_pages[0][0][0]=11;
 l->lrugen.nr_pages[1][0][0]=13;
 l->lrugen.nr_pages[2][0][0]=17;
 l->lrugen.nr_pages[3][0][1]=19;
 assert(mem_cgroup_get_lru_size(l,LRU_INACTIVE_ANON)==31);
 assert(mem_cgroup_get_lru_size(l,LRU_ACTIVE_ANON)==36);
 assert(mem_cgroup_get_zone_lru_size(l,LRU_ACTIVE_ANON,0)==17);
 assert(mem_cgroup_get_zone_lru_size(l,LRU_ACTIVE_ANON,1)==19);
 l->lrugen.nr_pages[0][1][0]=-5;
 l->lrugen.nr_pages[1][1][0]=9;
 assert(mem_cgroup_get_lru_size(l,LRU_INACTIVE_FILE)==4);
 m.lru_zone_size[0][LRU_UNEVICTABLE]=23;
 assert(mem_cgroup_get_lru_size(l,LRU_UNEVICTABLE)==23);
 l->lrugen.max_seq=4;
 assert(mem_cgroup_get_lru_size(l,LRU_ACTIVE_ANON)==30);
 assert(mem_cgroup_get_lru_size(l,LRU_INACTIVE_ANON)==37);
 puts("PASS: real 4.19 LRU readers include generations, zones, transition lists and signed batches");
}
'''

header = 'include/linux/memcontrol.h'
names = ('mem_cgroup_get_zone_lru_size', 'mem_cgroup_get_lru_size')

def extract(text, name):
    start = text.rindex('static inline', 0, text.index('unsigned long ' + name + '('))
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

cases = [('current', (source / header).read_text(), True)]
if len(sys.argv) > 1:
    original = subprocess.check_output(['git', '-C', str(source), 'show', sys.argv[1] + ':' + header], text=True)
    cases.append(('baseline', original, False))
for label, text, should_pass in cases:
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp) / 'test.c'
        c.write_text(PREFIX + '\n' +
                     '\n'.join(extract(text, name) for name in names) +
                     MAIN)
        binary = pathlib.Path(tmp) / 'test'
        subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(c), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert (result.returncode == 0) == should_pass, (label, result.stderr)
        print(label + ': ' + ('PASS' if should_pass else 'expected assertion failure'))

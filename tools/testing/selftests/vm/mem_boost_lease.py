#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Exercise actual launch-lease admission, expiry, wraparound and contention."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[4]
source = (root/'mm/mem_boost.c').read_text()


def extract(name):
    start = source.index('static ', source.index(name) - 32)
    brace = source.index('{', source.index(name))
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sys/types.h>
struct kobject { int unused; };
struct kobj_attribute { int unused; };
#define HZ 100
#define MEM_BOOST_DURATION (5 * HZ)
#define time_before(a,b) ((long)((a)-(b)) < 0)
static pthread_mutex_t mem_boost_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int mem_boost_mode;
static unsigned long mem_boost_expires, jiffies;
#define read_seqbegin(lock) (0U)
#define read_seqretry(lock,seq) ((void)(seq),false)
#define write_seqlock_irqsave(lock,flags) do { flags=0;pthread_mutex_lock(lock); } while(0)
#define write_sequnlock_irqrestore(lock,flags) do { (void)(flags);pthread_mutex_unlock(lock); } while(0)
static bool sysfs_streq(const char *a,const char *b) { return !strncmp(a,b,strlen(b)) && (!a[strlen(b)] || !strcmp(a+strlen(b),"\n")); }
static int kstrtouint(const char *s,int base,unsigned int *out) {
    char *end;errno=0;unsigned long v=strtoul(s,&end,base);
    if(end==s || (*end && strcmp(end,"\n")) || errno || v>UINT_MAX)return -EINVAL;
    *out=v;return 0;
}
'''
suffix = r'''
static ssize_t set(const char *value) { return mem_boost_mode_store(NULL,NULL,value,strlen(value)); }
static void *acquire(void *result) { *(ssize_t *)result=set("try2\n");return NULL; }
int main(void) {
    jiffies=1000;assert(set("try2\n")==5 && mem_boost_current_mode()==2);
    assert(mem_boost_expires==1500);
    jiffies=1499;assert(set("try2\n")==-EBUSY && mem_boost_expires==1500);
    jiffies=1500;assert(mem_boost_current_mode()==0 && set("try2\n")==5 && mem_boost_expires==2000);
    assert(set("0\n")==2 && mem_boost_current_mode()==0);
    assert(set("1\n")==2 && set("try2\n")==-EBUSY);
    assert(set("3\n")==2 && mem_boost_current_mode()==3);
    assert(set("4\n")==-EINVAL && set("bad")==-EINVAL && set("try3\n")==-EINVAL);
    assert(mem_boost_current_mode()==3);
    assert(set("0\n")==2);
    ssize_t a=0,b=0;pthread_t ta,tb;
    assert(!pthread_create(&ta,NULL,acquire,&a) && !pthread_create(&tb,NULL,acquire,&b));
    assert(!pthread_join(ta,NULL) && !pthread_join(tb,NULL));
    assert((a==5 && b==-EBUSY) || (b==5 && a==-EBUSY));
    jiffies=ULONG_MAX-200;assert(set("2\n")==2 && mem_boost_current_mode()==2);
    jiffies+=499;assert(mem_boost_current_mode()==2);
    jiffies++;assert(mem_boost_current_mode()==0);
    puts("PASS: lease acquisition, no renewal, numeric override, concurrent writers and jiffies wrap");
}
'''
with tempfile.TemporaryDirectory() as temp:
    c = pathlib.Path(temp)/'test.c'; exe = c.with_suffix('')
    c.write_text(prefix+extract('mem_boost_current_mode')+extract('mem_boost_mode_store')+suffix)
    subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)

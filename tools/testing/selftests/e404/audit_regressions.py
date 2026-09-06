#!/usr/bin/env python3
"""Compile actual changed functions with host fixtures; no device stress."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]

def source(path):
    return (ROOT / path).read_text()

def function(text, name):
    pos = text.index(name + '(')
    start = text.rfind('\n', 0, pos) + 1
    opening = text.index('{', pos)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

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
#define min_t(t,a,b) min((t)(a),(t)(b))
#define spin_lock_irqsave(lock,flags) ((void)(lock),(flags)=0)
#define spin_unlock_irqrestore(lock,flags) ((void)(lock),(void)(flags))
#define raw_spin_lock_irqsave spin_lock_irqsave
#define raw_spin_unlock_irqrestore spin_unlock_irqrestore
#define WARN_ON_ONCE(x) (x)
typedef uint64_t u64;
'''

thermal = PREFIX + r'''
enum thermal_pressure_source { THERMAL_PRESSURE_COOLING, THERMAL_PRESSURE_DCVSH,
                              THERMAL_PRESSURE_SOURCES };
struct cpumask { unsigned bits; };
static unsigned long thermal_sources[8][THERMAL_PRESSURE_SOURCES];
static unsigned long thermal_pressure[8];
static int thermal_pressure_lock;
#define per_cpu(name,cpu) name[cpu]
#define for_each_cpu(cpu,mask) for(cpu=0;cpu<8;cpu++) if ((mask)->bits & (1u<<cpu))
'''
text = source('drivers/base/arch_topology.c')
thermal += function(text, 'arch_set_thermal_pressure_source')
thermal += function(text, 'arch_set_thermal_pressure')
thermal += r'''
int main(void) {
    struct cpumask mask = {0x70};
    for (int order=0;order<2;order++) {
        int a=order, b=1-order;
        arch_set_thermal_pressure_source(&mask,300,a);
        arch_set_thermal_pressure_source(&mask,600,b);
        assert(thermal_pressure[4]==600 && thermal_pressure[0]==0);
        arch_set_thermal_pressure_source(&mask,0,b);
        assert(thermal_pressure[4]==300 && thermal_pressure[6]==300);
        arch_set_thermal_pressure_source(&mask,0,a);
        assert(thermal_pressure[4]==0);
    }
    arch_set_thermal_pressure(&mask,500);
    arch_set_thermal_pressure_source(&mask,700,THERMAL_PRESSURE_DCVSH);
    arch_set_thermal_pressure(&mask,0);
    assert(thermal_pressure[4]==700);
    return 0;
}
'''

walt = PREFIX + r'''
#define HZ 250
#define NSEC_PER_SEC 1000000000u
enum fps { FPS0=0,FPS60=60,FPS90=90,FPS120=120 };
static unsigned sysctl_sched_ravg_window_nr_ticks=5;
static unsigned display_sched_ravg_window_nr_ticks=5;
static unsigned sysctl_sched_dynamic_ravg_window_enable=1;
static unsigned new_sched_ravg_window=20000000;
static unsigned sched_display_refresh_rate;
static int sched_ravg_window_lock;
'''
text = source('kernel/sched/walt.c')
walt += function(text, 'sched_window_nr_ticks_change')
walt += function(text, 'sched_set_refresh_rate')
walt += r'''
int main(void) {
    sched_set_refresh_rate(FPS120); assert(new_sched_ravg_window==8000000);
    sched_set_refresh_rate(FPS90); assert(new_sched_ravg_window==12000000);
    sched_set_refresh_rate(FPS60); assert(new_sched_ravg_window==20000000);
    sysctl_sched_ravg_window_nr_ticks=2;
    sched_set_refresh_rate(FPS60); assert(new_sched_ravg_window==8000000);
    assert(sysctl_sched_ravg_window_nr_ticks==2);
    sysctl_sched_dynamic_ravg_window_enable=0;
    sysctl_sched_ravg_window_nr_ticks=5;
    sched_set_refresh_rate(FPS90); assert(new_sched_ravg_window==8000000);
    assert(sched_display_refresh_rate==90);
    sysctl_sched_dynamic_ravg_window_enable=1;
    sched_window_nr_ticks_change(); assert(new_sched_ravg_window==12000000);
    sched_set_refresh_rate(FPS0); assert(sched_display_refresh_rate==90);
    return 0;
}
'''

watchdog = PREFIX + r'''
struct scheduler_ctx { void *sch_thread; unsigned sch_event_flag; };
static bool recovering;
static unsigned requested, notified, traced;
#define MC_SHUTDOWN_EVENT_MASK 1
#define QDF_REASON_UNSPECIFIED 0
#define qdf_is_recovering() recovering
#define sched_debug(...) ((void)0)
#define sched_err(...) ((void)0)
#define qdf_atomic_test_bit(bit,flag) (*(flag)&(bit))
#define qdf_trigger_self_recovery(psoc,reason) ((void)(psoc),(void)(reason),requested++)
static void scheduler_watchdog_notify(struct scheduler_ctx *s) { (void)s; notified++; }
static void qdf_print_thread_trace(void *s) { (void)s; traced++; }
'''
watchdog += function(source('drivers/staging/qca-wifi-host-cmn/scheduler/src/scheduler_api.c'), 'scheduler_watchdog_timeout')
watchdog += r'''
int main(void) {
    struct scheduler_ctx s = {(void*)1,0};
    scheduler_watchdog_timeout(&s); assert(requested==1 && notified==1 && traced==1);
    recovering=true; scheduler_watchdog_timeout(&s); assert(requested==1 && notified==1);
    recovering=false; s.sch_event_flag=1; scheduler_watchdog_timeout(&s); assert(requested==1);
    s.sch_event_flag=0; s.sch_thread=NULL; scheduler_watchdog_timeout(&s);
    assert(requested==2 && traced==2);
    return 0;
}
'''

cass = PREFIX + r'''
#define CONFIG_SCHED_WALT
#define SCHED_CAPACITY_SCALE 1024
#define UCLAMP_MIN 0
#undef __always_inline
#define __always_inline inline
#define unlikely(x) (x)
#define rcu_dereference(x) (x)
#define per_cpu(name,cpu) name[cpu]
#define for_each_cpu_and(cpu,a,b) for(cpu=0;cpu<8;cpu++) if ((*(a)&*(b)) & (1u<<cpu))
#define fits_capacity(util,cap) ((util)*1280 < (cap)*1024)
static int nr_cpu_ids=8;
struct task_struct { unsigned cpus_allowed; unsigned long util, uc_min; bool latency; int cpu; };
struct cpuidle_state { unsigned exit_latency; };
struct rq { struct { unsigned long cumulative_runnable_avg_scaled; } walt_stats;
            unsigned long thermal; int nr_running; };
static struct rq rqs[8];
static unsigned long caps[8], mins[8];
static unsigned active=255;
static unsigned *cpu_active_mask=&active;
static int sd_llc[8], sd_llc_size[8], sd_llc_id[8];
static struct task_struct current_task;
static struct task_struct *current=&current_task;
#define cpu_rq(cpu) (&rqs[cpu])
#define task_util(p) ((p)->util)
#define task_util_est(p) ((p)->util)
#define task_cpu(p) ((p)->cpu)
#define rt_task(p) ((void)(p),false)
#define cpu_util_irq(rq) ((void)(rq),0UL)
#define raw_smp_processor_id() 0
#define uclamp_eff_value(p,u) ((void)(u),(p)->uc_min)
#define uclamp_latency_sensitive(p) ((p)->latency)
#define arch_scale_cpu_capacity(cpu) caps[cpu]
#define arch_scale_min_freq_capacity(cpu) mins[cpu]
#define thermal_load_avg(rq) ((rq)->thermal)
#define choose_idle_cpu(cpu,p) ((void)(p),rqs[cpu].nr_running==0)
#define idle_get_state(rq) ((void)(rq),(struct cpuidle_state*)NULL)
'''
text = source('kernel/sched/cass.c')
cass += text[text.index('struct cass_cpu_cand {'):text.index('static int cass_select_task_rq(')]
cass += r'''
int main(void) {
    struct task_struct p={.cpus_allowed=0x81,.util=100,.uc_min=800,.cpu=0};
    for(int i=0;i<8;i++) { caps[i]=512; mins[i]=200; rqs[i].nr_running=1; }
    caps[0]=caps[7]=1024; rqs[7].nr_running=0;
    assert(cass_best_cpu(&p,0,false,false)==0);
    p.latency=true; assert(cass_best_cpu(&p,0,false,false)==7);
    p.cpus_allowed=1; rqs[0].thermal=2048;
    assert(cass_best_cpu(&p,0,false,false)==0);
    p.cpus_allowed=0x81; rqs[0].thermal=0; rqs[7].thermal=600;
    assert(cass_best_cpu(&p,0,false,false)==0);
    p.cpus_allowed=0x80;
    assert(cass_best_cpu(&p,7,false,false)==7);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='e404-audit-') as directory:
    for name, fixture in [('thermal',thermal),('walt',walt),('watchdog',watchdog),('cass',cass)]:
        path=Path(directory)/f'{name}.c'; path.write_text(fixture)
        output=path.with_suffix('')
        subprocess.run([os.getenv('CC','clang'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined',str(path),'-o',str(output)],check=True)
        subprocess.run([str(output)],check=True)
        print(f'{name}: actual-source assertions passed',flush=True)

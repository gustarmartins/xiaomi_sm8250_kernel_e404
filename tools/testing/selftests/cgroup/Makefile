# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wall

all:

TEST_GEN_PROGS = test_memcontrol
TEST_GEN_PROGS += test_core
TEST_GEN_PROGS += test_kill

include ../lib.mk

$(OUTPUT)/test_memcontrol: cgroup_util.c
$(OUTPUT)/test_core: cgroup_util.c
$(OUTPUT)/test_kill: cgroup_util.c

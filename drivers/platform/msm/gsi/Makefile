# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_GSI) += gsi.o
# gsi.c only carries a !CONFIG_DEBUG_FS stub of gsi_debugfs_init();
# the real one lives in gsi_dbg.c.
ifdef CONFIG_DEBUG_FS
obj-$(CONFIG_GSI) += gsi_dbg.o
endif

obj-$(CONFIG_IPA_EMULATION) += gsi_emulation.o

# SPDX-License-Identifier: MIT
#
# Copyright(c) 2020 Intel Corporation.
#
# Intel Connectivity fabric driver
#
#

obj-$(CPTCFG_DRM_I915_INTEL_FABRIC) += iaf.o

iaf-y := debugfs.o \
	 fw.o \
	 main.o \
	 mei_iaf_user.o \
	 mbdb.o \
	 netlink.o \
	 ops.o \
	 parallel.o \
	 port.o \
	 routing_engine.o \
	 routing_event.o \
	 routing_io.o \
	 routing_logic.o \
	 routing_p2p.o \
	 routing_pause.o \
	 routing_topology.o \
	 trace.o \

ifdef CPTCFG_MBOX_ACCESS
iaf-y += mbox.o
endif

#export SELFTESTS=1
ifdef SELFTESTS
ccflags-y += -DSELFTESTS -I$(src)
iaf-y += selftests/selftest.o \
	 selftests/routing_selftest.o \
	 selftests/routing_mock.o \

endif

CFLAGS_trace.o = -I$(src)

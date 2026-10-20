.DEFAULT_GOAL := all
ifeq ($(LEGACY_EPICS_BUILD),1)
include Makefile.epics
else
QT_MODULES :=
EPICS_REQUIRED_LIBS := ca Com
MOTIF_REQUIRED := 1
include ../Makefile.rules
ifneq ($(OS),Windows)
# Preserve Probe's legacy C dialect and warning policy.
CFLAGS := $(filter-out -Wall,$(CFLAGS)) -std=gnu99 -Wno-deprecated-declarations
endif
CPPFLAGS += $(EPICS_INCLUDES) -I$(MOTIF_INC) -I$(X11_INC)
# Match the original source list; the optional serial dial driver and unused
# alternative options dialog are preserved but were never linked by Probe.
SOURCES = $(filter-out probeOption.c probeDial.c,$(wildcard *.c))
PROD = probe
probe_SRC = $(SOURCES)
probe_LIBS = $(CA_LIBS) $(XM_LIB) $(MOTIF_STATIC_DEP_LIBS) $(MOTIF_STATIC_EXTRA_LIBS) $(XMU_LIB) $(XT_LIB) $(X11_LIB) $(EPICS_SYS_LIBS)
LDFLAGS += $(call epics_rpath,$(CA_LIBS),$(EPICS_BASE_LIB_DIR))
EXTRA_CXX_OBJ_DEPS = probe.h probeVersion.h
include ../Makefile.build
$(probe_OBJS): probe.h probeVersion.h
endif

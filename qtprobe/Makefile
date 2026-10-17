.DEFAULT_GOAL := all
include ../Makefile.rules
OBJ_DIR := O.$(OS)-$(ARCH)-qt$(QT_VERSION)
SOURCES = model.cc channel.cc ca_connection.cc pva_connection.cc inspector.cc comparison.cc record.cc record_explorer.cc structure_browser.cc pv_drop.cc motif_pv_drop.cc help.cc about_banner.cc appearance.cc
OBJEXT := $(if $(filter Windows,$(OS)),obj,o)
EXEEXT := $(if $(filter Windows,$(OS)),.exe)
OBJECTS = $(addprefix $(OBJ_DIR)/,$(SOURCES:.cc=.$(OBJEXT))) $(OBJ_DIR)/moc_channel.$(OBJEXT) $(OBJ_DIR)/resources.$(OBJEXT)
HEADERS = $(wildcard *.h)
CPPFLAGS += -I. -I$(OBJ_DIR) $(EPICS_INCLUDES) -DQTPROBE_HAVE_PVA=$(HAVE_PVA)
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c++17 -fPIC
LIBS = $(if $(filter 1,$(HAVE_PVA)),$(PVA_LIBS)) $(CA_LIBS) $(EPICS_SYS_LIBS) $(QT_LIBS)
# Shared EPICS libraries need a run path; static ones (the default) must not add
# one, or every launch searches that directory first for Qt and system libraries.
comma := ,
LDFLAGS += $(if $(filter %.$(EPICS_SHARED_EXT),$(CA_LIBS) $(PVA_LIBS)),-Wl$(comma)-rpath$(comma)$(EPICS_LIB_DIR))
# Legacy MEDM's Motif drop protocol is available on X11 without linking Motif.
ifeq ($(OS),Linux)
ifneq ($(shell $(PKG_CONFIG) --exists xcb && echo yes),)
CPPFLAGS += -DQTPROBE_HAVE_XCB $(shell $(PKG_CONFIG) --cflags xcb)
QT_LIBS += $(shell $(PKG_CONFIG) --libs xcb)
endif
endif
ifeq ($(OS),Windows)
CXX := cl
CXXFLAGS = /nologo /O2 /W3 /external:anglebrackets /external:W0 /MD /EHsc /permissive- /Zc:lambda /Zc:__cplusplus /utf-8 /std:c++17
# Older EPICS pvData headers need delayed template lookup only in the PVA unit.
$(OBJ_DIR)/pva_connection.obj: CXXFLAGS += /Zc:twoPhase-
CPPFLAGS += -DEPICS_CALL_DLL -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS
LDFLAGS :=
define compile
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(1) /c $< /Fo$@
endef
define link
	link /nologo /OUT:$@ $^ $(LIBS) $(1)
endef
# Explicit file logging also works when Windows has no attached console.
define run_test
	QT_QPA_PLATFORM=offscreen $(1) -o $(OBJ_DIR)/$(2).txt,txt; status=$$?; cat $(OBJ_DIR)/$(2).txt; exit $$status
endef
else
define compile
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(1) -MMD -MP -c $< -o $@
endef
define link
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS) $(1)
endef
define run_test
	QT_QPA_PLATFORM=offscreen $(1)
endef
endif
.PHONY: all install check test test-core test-ui test-ioc clean distclean
all: check install
check:
	@test -n "$(HAVE_QT)" || { echo 'Qt 5.15 or Qt 6 Widgets development files required.'; exit 1; }
	@test -f "$(EPICS_BASE)/include/cadef.h" || { echo 'Set EPICS_BASE to a built EPICS Base.'; exit 1; }
	@echo "QtProbe: Qt $(QT_VERSION), EPICS $(EPICS_BASE), pvAccess=$(HAVE_PVA)"
$(OBJ_DIR):
	mkdir -p $@
$(OBJ_DIR)/%.$(OBJEXT): %.cc $(HEADERS) Makefile ../Makefile.rules | $(OBJ_DIR)
	$(call compile,)
$(OBJ_DIR)/moc_channel.cc: channel.h Makefile ../Makefile.rules | $(OBJ_DIR)
	$(MOC) $(CPPFLAGS) $(QT_CFLAGS) $< -o $@
$(OBJ_DIR)/moc_channel.$(OBJEXT): $(OBJ_DIR)/moc_channel.cc Makefile ../Makefile.rules
	$(call compile,)
$(OBJ_DIR)/resources.cc: resources/qtprobe.qrc $(wildcard resources/*.svg) ../docs/qtprobe-guide.md | $(OBJ_DIR)
	$(RCC) $< -o $@
$(OBJ_DIR)/resources.$(OBJEXT): $(OBJ_DIR)/resources.cc Makefile ../Makefile.rules
	$(call compile,)
$(OBJ_DIR)/qtprobe$(EXEEXT): $(OBJECTS) $(OBJ_DIR)/main.$(OBJEXT)
	$(call link,)
install: $(OBJ_DIR)/qtprobe$(EXEEXT)
	@mkdir -p $(BIN_DIR) $(EPICS_BIN_DIR)
	cp -f $< $(BIN_DIR)/
	$(if $(EPICS_BIN_DIR),cp -f $< $(EPICS_BIN_DIR)/)
$(OBJ_DIR)/test_%.moc: tests/test_%.cc Makefile ../Makefile.rules | $(OBJ_DIR)
	$(MOC) $(CPPFLAGS) $(QT_CFLAGS) $(QT_TEST_CFLAGS) $< -o $@
$(OBJ_DIR)/test_%.$(OBJEXT): tests/test_%.cc $(OBJ_DIR)/test_%.moc $(HEADERS) $(wildcard tests/*.h) Makefile ../Makefile.rules
	$(call compile,$(QT_TEST_CFLAGS) -DEPICS_TEST_BIN='"$(EPICS_BASE)/bin/$(EPICS_HOST_ARCH)"')
$(OBJ_DIR)/test_%$(EXEEXT): $(OBJ_DIR)/test_%.$(OBJEXT) $(OBJECTS)
	$(call link,$(QT_TEST_LIBS))
test: test-core test-ui test-ioc
test-core: $(OBJ_DIR)/test_core$(EXEEXT)
	$(call run_test,$<,test-core)
$(OBJ_DIR)/striptool_recorder.$(OBJEXT): tests/striptool_recorder.cc Makefile ../Makefile.rules | $(OBJ_DIR)
	$(call compile,)
$(OBJ_DIR)/striptool_recorder$(EXEEXT): $(OBJ_DIR)/striptool_recorder.$(OBJEXT)
	$(call link,)
test-ui: $(OBJ_DIR)/test_ui$(EXEEXT) $(OBJ_DIR)/striptool_recorder$(EXEEXT)
	$(call run_test,$<,test-ui)
.PHONY: benchmark-cpu
$(OBJ_DIR)/benchmark_cpu.$(OBJEXT): tests/benchmark_cpu.cc $(HEADERS) | $(OBJ_DIR)
	$(call compile,)
$(OBJ_DIR)/benchmark_cpu$(EXEEXT): $(OBJ_DIR)/benchmark_cpu.$(OBJEXT) $(OBJECTS)
	$(call link,$(if $(filter Windows,$(OS)),Psapi.lib))
benchmark-cpu: $(OBJ_DIR)/benchmark_cpu$(EXEEXT)
	QT_QPA_PLATFORM=offscreen $< --pvs 0 --period 0
	QT_QPA_PLATFORM=offscreen $< --pvs 100 --period 0
	QT_QPA_PLATFORM=offscreen $< --pvs 100 --period 100 --history 10000
	QT_QPA_PLATFORM=offscreen $< --pvs 1 --period 100 --history 10000 --view inspector
	QT_QPA_PLATFORM=offscreen $< --pvs 1 --period 100 --history 10000 --view trend
# Optional X11 integration tests. They inject synthetic pointer input, so they
# always run on a private Xvfb display and never on the user's desktop.
XVFB_RUN ?= xvfb-run -a -s '-screen 0 1920x1080x24 -nolisten tcp'
require_xvfb = @command -v xvfb-run > /dev/null || { echo '$@ needs xvfb-run (Xvfb); it never uses your display.'; exit 1; }
QTEDM_TEST_PROGRAM ?= $(abspath ../../medm/bin/$(OS)-$(ARCH)/qtedm)
.PHONY: test-qtedm-drop
$(OBJ_DIR)/x11_drag_driver: tests/x11_drag_driver.c | $(OBJ_DIR)
	$(CC) $< -o $@ -lX11 -lXtst
test-qtedm-drop: $(OBJ_DIR)/test_ui$(EXEEXT) $(OBJ_DIR)/x11_drag_driver
	$(require_xvfb)
	$(XVFB_RUN) env QT_QPA_PLATFORM=xcb QTPROBE_TEST_QTEDM="$(QTEDM_TEST_PROGRAM)" \
	 QTPROBE_TEST_DRAG_DRIVER="$(abspath $(OBJ_DIR)/x11_drag_driver)" \
	 $(OBJ_DIR)/test_ui qtedmMiddleButtonDrop
# Real Motif interoperability check, also on a private Xvfb display.
.PHONY: test-motif
$(OBJ_DIR)/motif_drag_source: tests/motif_drag_source.c | $(OBJ_DIR)
	$(CC) $(addprefix -I,$(MOTIF_INC) $(X11_INC)) $< -o $@ -lXm -lXt -lX11 -lXtst
test-motif: $(OBJ_DIR)/test_ui$(EXEEXT) $(OBJ_DIR)/motif_drag_source
	$(require_xvfb)
	$(XVFB_RUN) env QT_QPA_PLATFORM=xcb QTPROBE_TEST_MOTIF_SOURCE="$(abspath $(OBJ_DIR)/motif_drag_source)" \
	 $(OBJ_DIR)/test_ui motifMiddleButtonDrop
test-ioc: $(OBJ_DIR)/test_ioc$(EXEEXT) $(OBJ_DIR)/qtprobe$(EXEEXT)
	$(call run_test,$<,test-ioc)
clean:
	rm -rf O.$(OS)-$(ARCH)-qt5 O.$(OS)-$(ARCH)-qt6
distclean: clean
	rm -f $(BIN_DIR)/qtprobe$(EXEEXT)
-include $(OBJECTS:.$(OBJEXT)=.d) $(OBJ_DIR)/main.d $(wildcard $(OBJ_DIR)/test_*.d)
.PRECIOUS: $(OBJ_DIR)/test_%.$(OBJEXT) $(OBJ_DIR)/test_%.moc

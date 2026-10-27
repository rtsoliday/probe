.DEFAULT_GOAL := all
QT_REQUIRED := 1
QT_MODULES := Core Gui Widgets Network
EPICS_REQUIRED_LIBS := ca Com
EPICS_OPTIONAL_LIBS := pvAccess pvData
include ../Makefile.rules
OBJ_DIR := O.$(OS)-$(ARCH)-qt$(QT_VERSION)
SOURCES = sdds_export.cc model.cc channel.cc ca_connection.cc pva_connection.cc inspector.cc comparison.cc record.cc record_explorer.cc structure_browser.cc pv_drop.cc motif_pv_drop.cc help.cc about_banner.cc appearance.cc
OBJEXT := $(if $(filter Windows,$(OS)),obj,o)
EXEEXT := $(if $(filter Windows,$(OS)),.exe)
OBJECTS = $(addprefix $(OBJ_DIR)/,$(SOURCES:.cc=.$(OBJEXT))) $(OBJ_DIR)/moc_channel.$(OBJEXT) $(OBJ_DIR)/resources.$(OBJEXT)
HEADERS = $(wildcard *.h)
# moc gets only these flags; compiler options from an environment CPPFLAGS would stop it.
QTPROBE_CPPFLAGS = -I. -I$(OBJ_DIR) $(EPICS_INCLUDES) -DQTPROBE_HAVE_PVA=$(HAVE_PVA)
CPPFLAGS += $(QTPROBE_CPPFLAGS)
CXXFLAGS += $(if $(filter-out Windows,$(OS)),-Wno-unused-parameter)
LIBS = $(if $(filter 1,$(HAVE_PVA)),$(PVA_LIBS)) $(CA_LIBS) $(EPICS_SYS_LIBS) $(QT_LIBS) $(LDLIBS)
# Shared EPICS libraries need a run path; static ones (the default) must not add
# one, or every launch searches that directory first for Qt and system libraries.
comma := ,
LDFLAGS += $(if $(filter %.$(EPICS_SHARED_EXT),$(CA_LIBS) $(PVA_LIBS)),-Wl$(comma)-rpath$(comma)$(EPICS_BASE_LIB_DIR))
# Legacy MEDM's Motif drop protocol is available on X11 without linking Motif.
ifeq ($(OS),Linux)
ifneq ($(shell $(PKG_CONFIG) --exists xcb && echo yes),)
QTPROBE_CPPFLAGS += -DQTPROBE_HAVE_XCB $(shell $(PKG_CONFIG) --cflags xcb)
QT_LIBS += $(shell $(PKG_CONFIG) --libs xcb)
endif
endif
# Library prerequisites trigger relinks; LIBS supplies them in link order.
ifeq ($(OS),Windows)
CXXFLAGS = /nologo /O2 /W3 /external:anglebrackets /external:W0 /MD /EHsc /permissive- /Zc:lambda /Zc:__cplusplus /utf-8 $(CXX_STD_FLAGS)
# Older EPICS pvData headers need delayed template lookup only in the PVA unit.
$(OBJ_DIR)/pva_connection.obj: CXXFLAGS += /Zc:twoPhase-
QTPROBE_CPPFLAGS += -DEPICS_CALL_DLL -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS
LDFLAGS :=
define compile
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(1) /c $< /Fo$@
endef
define link
	link /nologo $(LDFLAGS) /OUT:$@ $(filter %.$(OBJEXT),$^) $(LIBS) $(1)
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
	$(CXX) $(LDFLAGS) -o $@ $(filter %.$(OBJEXT),$^) $(LIBS) $(1)
endef
define run_test
	QT_QPA_PLATFORM=offscreen $(1)
endef
endif
# Optional SDDS: try complete installations in priority order. Explicit include
# or library overrides are authoritative, including nonexistent paths (useful
# for building/testing without SDDS). Never borrow a core library from another
# installation. The link probe is compiled, never executed, and stays quiet.
SDDS_REPO ?= $(abspath $(RULES_DIR)../SDDS)
ifneq ($(strip $(SDDS_INCLUDE_DIR) $(SDDS_LIB_DIR)),)
SDDS_CANDIDATES := explicit
else
SDDS_CANDIDATES := sibling extensions aps
endif
sdds_include_explicit = $(SDDS_INCLUDE_DIR)
sdds_library_explicit = $(SDDS_LIB_DIR)
sdds_include_sibling = $(SDDS_REPO)/include
sdds_library_sibling = $(SDDS_REPO)/lib/$(OS)-$(ARCH)
sdds_include_extensions = $(or $(EXTENSIONS_INCLUDE_DIR),$(EPICS_INC_DIR))
sdds_library_extensions = $(or $(EXTENSIONS_LIB_DIR),$(EPICS_LIB_DIR))
sdds_include_aps = /usr/local/epics/extensions/include
sdds_library_aps = /usr/local/epics/extensions/lib/$(EPICS_HOST_ARCH)
sdds_lib = $(if $(strip $(2)),$(if $(filter Windows,$(OS)),$(wildcard $(2)/$(1).lib),$(call find_lib,$(1),a dylib so,$(2))))
sdds_files = $(foreach lib,SDDS1 rpnlib mdbmth mdblib,$(call sdds_lib,$(lib),$(sdds_library_$(1)))) \
             $(or $(call sdds_lib,lzma,$(sdds_library_$(1))),$(LZMA_LIB)) \
             $(or $(call sdds_lib,z,$(sdds_library_$(1))),$(Z_LIB))
sdds_includes = $(if $(filter Windows,$(OS)),/I"$(sdds_include_$(1))" /I"$(sdds_include_$(1))/../lzma",-I"$(sdds_include_$(1))")
sdds_windows_gsl = $(foreach lib,gsl gslcblas,$(or $(call sdds_lib,$(lib),$(sdds_library_$(1))),$(call sdds_lib,$(lib),$(WINDOWS_GSL_LIB_DIR))))
sdds_links = $(foreach lib,$(call sdds_files,$(1)),"$(lib)") \
             $(if $(filter Windows,$(OS)),$(foreach lib,$(call sdds_windows_gsl,$(1)),"$(lib)") /LIBPATH:"$(sdds_library_$(1))" $(LIB_LINK_DIRS),$(GSL_LINK_LIBS) -lm $(call epics_rpath,$(call sdds_files,$(1)),$(sdds_library_$(1))))
ifeq ($(OS),Windows)
sdds_probe_command = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(call sdds_includes,$(1)) build/sdds_link_probe.cc /Fo"$(OBJ_DIR)/sdds-probe.obj" /Fe"$(OBJ_DIR)/sdds-probe.exe" /link $(LDFLAGS) $(call sdds_links,$(1))
else
sdds_probe_command = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(call sdds_includes,$(1)) build/sdds_link_probe.cc $(LDFLAGS) -o "$(OBJ_DIR)/sdds-probe" $(call sdds_links,$(1))
endif
sdds_probe = $(if $(and $(wildcard $(sdds_include_$(1))/SDDS.h),$(call sdds_lib,SDDS1,$(sdds_library_$(1)))),$(shell mkdir -p "$(OBJ_DIR)"; $(call sdds_probe_command,$(1)) > "$(OBJ_DIR)/sdds-probe-$(1).log" 2>&1 && echo yes))
sdds_first_usable = $(if $(strip $(1)),$(if $(call sdds_probe,$(firstword $(1))),$(firstword $(1)),$(call sdds_first_usable,$(wordlist 2,$(words $(1)),$(1)))))
ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
# Probe on each invocation so replaced headers/libraries and toolchain changes
# are recognized without a clean build, including recovery from a failed probe.
SDDS_SELECTED := $(call sdds_first_usable,$(SDDS_CANDIDATES))
endif
ifneq ($(SDDS_SELECTED),)
SDDS_LINK_LIBS := $(call sdds_links,$(SDDS_SELECTED))
SDDS_DEPENDENCIES := $(call sdds_files,$(SDDS_SELECTED)) $(if $(filter Windows,$(OS)),$(call sdds_windows_gsl,$(SDDS_SELECTED)),$(wildcard $(filter-out -%,$(GSL_LINK_LIBS))))
SDDS_HEADERS := $(wildcard $(sdds_include_$(SDDS_SELECTED))/SDDS*.h)
QTPROBE_CPPFLAGS += -DQTPROBE_HAVE_SDDS
CPPFLAGS += $(call sdds_includes,$(SDDS_SELECTED))
endif

LIBS += $(SDDS_LINK_LIBS)

.PHONY: all install check test test-core test-ui test-ioc clean distclean
all: check
check:
	@test -n "$(HAVE_QT)" || { echo 'Qt 5.15 or Qt 6 Widgets development files required.'; exit 1; }
	@test -f "$(EPICS_BASE)/include/cadef.h" || { echo 'Set EPICS_BASE to a built EPICS Base.'; exit 1; }
	@echo "QtProbe: Qt $(QT_VERSION), EPICS $(EPICS_BASE), pvAccess=$(HAVE_PVA)"
$(OBJ_DIR)/%.$(OBJEXT): %.cc $(HEADERS) $(BUILD_FILES) | $(OBJ_DIR)
	$(call compile,)
$(OBJ_DIR)/moc_channel.cc: channel.h $(BUILD_FILES) | $(OBJ_DIR)
	$(MOC) $(QTPROBE_CPPFLAGS) $(QT_CFLAGS) $< -o $@
$(OBJ_DIR)/moc_channel.$(OBJEXT): $(OBJ_DIR)/moc_channel.cc $(BUILD_FILES)
	$(call compile,)
$(OBJ_DIR)/resources.cc: resources/qtprobe.qrc $(wildcard resources/*.svg) $(BUILD_FILES) ../docs/qtprobe-guide.md | $(OBJ_DIR)
	$(RCC) $< -o $@
$(OBJ_DIR)/resources.$(OBJEXT): $(OBJ_DIR)/resources.cc $(BUILD_FILES)
	$(call compile,)
$(OBJ_DIR)/qtprobe$(EXEEXT): $(OBJECTS) $(OBJ_DIR)/main.$(OBJEXT)
	$(call link,)
PROGRAM = $(OBJ_DIR)/qtprobe$(EXEEXT)
install: install-program
CUSTOM_COMPILE_RULES := 1
CUSTOM_CLEAN := 1
PROD = qtprobe
qtprobe_OBJS = $(OBJECTS) $(OBJ_DIR)/main.$(OBJEXT)
qtprobe_LIBS = $(LIBS)
qtprobe_DEPS = $(SDDS_DEPENDENCIES)
BUILD_CONFIG_VARIABLES += SDDS_SELECTED SDDS_LINK_LIBS SDDS_REPO SDDS_INCLUDE_DIR SDDS_LIB_DIR
BUILD_OBJECTS += $(addprefix $(OBJ_DIR)/,test_sdds.$(OBJEXT) test_core.$(OBJEXT) test_ui.$(OBJEXT) test_ioc.$(OBJEXT) striptool_recorder.$(OBJEXT) benchmark_cpu.$(OBJEXT))
BUILD_GENERATED += $(OBJ_DIR)/moc_channel.cc $(OBJ_DIR)/resources.cc $(addprefix $(OBJ_DIR)/,test_sdds.moc test_core.moc test_ui.moc test_ioc.moc)
CUSTOM_LINK_PRODUCTS += qtprobe
include ../Makefile.build
$(OBJ_DIR)/sdds_export.$(OBJEXT) $(OBJ_DIR)/test_sdds.$(OBJEXT): $(SDDS_HEADERS)

$(OBJ_DIR)/test_%.moc: tests/test_%.cc $(BUILD_FILES) | $(OBJ_DIR)
	$(MOC) $(QTPROBE_CPPFLAGS) $(QT_CFLAGS) $(QT_TEST_CFLAGS) $< -o $@
$(OBJ_DIR)/test_%.$(OBJEXT): tests/test_%.cc $(OBJ_DIR)/test_%.moc $(HEADERS) $(wildcard tests/*.h) $(BUILD_FILES)
	$(call compile,$(QT_TEST_CFLAGS) -DEPICS_TEST_BIN='"$(EPICS_BASE)/bin/$(EPICS_HOST_ARCH)"')
$(OBJ_DIR)/test_%$(EXEEXT): $(OBJ_DIR)/test_%.$(OBJEXT) $(OBJECTS) $(SDDS_DEPENDENCIES)
	$(call link,$(QT_TEST_LIBS))
.PHONY: test-sdds
test-sdds: $(OBJ_DIR)/test_sdds$(EXEEXT)
	$(call run_test,$<,test-sdds)
test: test-core test-ui test-sdds test-ioc
test-core: $(OBJ_DIR)/test_core$(EXEEXT)
	$(call run_test,$<,test-core)
$(OBJ_DIR)/striptool_recorder.$(OBJEXT): tests/striptool_recorder.cc $(BUILD_FILES) | $(OBJ_DIR)
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
	rm -rf O.$(OS)-$(ARCH)-qt5 O.$(OS)-$(ARCH)-qt6 O.$(OS)-$(ARCH)-qt5-dev O.$(OS)-$(ARCH)-qt6-dev
distclean: clean
	rm -f $(BIN_DIR)/qtprobe$(EXEEXT)
-include $(OBJECTS:.$(OBJEXT)=.d) $(OBJ_DIR)/main.d $(wildcard $(OBJ_DIR)/test_*.d)
.PRECIOUS: $(OBJ_DIR)/test_%.$(OBJEXT) $(OBJ_DIR)/test_%.moc

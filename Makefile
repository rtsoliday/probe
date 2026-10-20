# Probe and QtProbe build dispatcher, following QtALH and Qt StripTool.
.DEFAULT_GOAL := all
QT_MODULES := Core Gui Widgets Network
# Discover Base here; individual components enforce their own requirements.
EPICS_OPTIONAL_LIBS := ca Com
include Makefile.rules
.PHONY: all probe qtprobe test test-qtprobe test-ioc install clean distclean
all: qtprobe $(if $(HAVE_MOTIF),probe)
ifeq ($(HAVE_MOTIF),)
	@echo "Motif development files unavailable; built QtProbe only."
endif
probe:
ifeq ($(OS),Windows)
	@echo "Legacy Probe requires Unix, Motif and X11; use make qtprobe on Windows."
	@exit 1
else
	+$(MAKE) -C probe
endif
qtprobe:
	+$(MAKE) -C qtprobe
test test-qtprobe:
	+$(MAKE) -C qtprobe test
test-ioc:
	+$(MAKE) -C qtprobe test-ioc
install: all
clean:
	+$(MAKE) -C probe $@
	+$(MAKE) -C qtprobe $@
distclean:
	+$(MAKE) -C probe $@
	+$(MAKE) -C qtprobe $@
	@# rmdir removes the binary directories only when nothing else is in them.
	-@rmdir bin/*/ bin 2>/dev/null || true

.DEFAULT_GOAL := all
ifeq ($(LEGACY_EPICS_BUILD),1)
include Makefile.epics
else
include ../Makefile.rules
CFLAGS ?= -O2 -g -std=gnu99 -Wno-deprecated-declarations
CPPFLAGS += $(EPICS_INCLUDES) -I$(MOTIF_INC) -I$(X11_INC)
# Match the original source list; the optional serial dial driver and unused
# alternative options dialog are preserved but were never linked by Probe.
SOURCES = $(filter-out probeOption.c probeDial.c,$(wildcard *.c))
OBJECTS = $(addprefix $(OBJ_DIR)/,$(SOURCES:.c=.o))
.PHONY: all install clean distclean
all: install
$(OBJ_DIR)/%.o: %.c probe.h probeVersion.h Makefile ../Makefile.rules
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(OBJ_DIR)/probe: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(CA_LIBS) $(EPICS_SYS_LIBS) -Wl,-rpath,$(EPICS_LIB_DIR) -L$(MOTIF_LIB) -L$(X11_LIB) -lXm -lXt -lX11
install: $(OBJ_DIR)/probe
	@mkdir -p $(BIN_DIR) $(EPICS_BIN_DIR)
	cp -f $< $(BIN_DIR)/probe
	$(if $(EPICS_BIN_DIR),cp -f $< $(EPICS_BIN_DIR)/probe)
clean:
	rm -rf O.$(OS)-$(ARCH)
distclean: clean
	rm -f $(BIN_DIR)/probe
-include $(OBJECTS:.o=.d)
endif

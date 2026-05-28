# GSynth EH Micro Synthesizer - LV2 plug-in
#
# Build local :          make
# Build cross (MOD)  :   make CC=$(TARGET_CC) avec PKG_CONFIG_PATH visant
#                        le sysroot ; mod-plugin-builder fournit déjà ces vars.
# Installation :         make install [DESTDIR=...] [PREFIX=/usr] [LV2DIR=...]
# Pour MOD (Buildroot) : cp -rL eh_micro_synth.lv2 $(TARGET_DIR)/usr/lib/lv2/

BUNDLE := eh_micro_synth.lv2
PLUGIN := eh_micro_synth
SO     := $(BUNDLE)/$(PLUGIN).so

CC ?= cc
PKG_CONFIG ?= pkg-config

OPTFLAGS ?= -O2 -ffast-math

# Append-only : on respecte les CFLAGS/LDFLAGS injectés par l'environnement
# (mod-plugin-builder, Buildroot, etc.) tout en ajoutant nos besoins.
override CFLAGS  += $(OPTFLAGS) -fPIC -Wall -Wextra -Wno-unused-parameter \
                    $(shell $(PKG_CONFIG) --cflags lv2)
override LDFLAGS += -shared -fvisibility=hidden
LDLIBS ?= -lm

PREFIX ?= /usr/local
LV2DIR ?= $(PREFIX)/lib/lv2

all: $(SO)

$(SO): src/$(PLUGIN).c | $(BUNDLE)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUNDLE):
	mkdir -p $@

smoke: $(SO)
	$(CC) -O2 -Wall src/smoke_test.c -ldl -lm -o /tmp/ehms_smoke
	/tmp/ehms_smoke $(SO)

clean:
	rm -f $(SO) /tmp/ehms_smoke

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m 644 $(BUNDLE)/manifest.ttl  $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	install -m 644 $(BUNDLE)/$(PLUGIN).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	install -m 755 $(BUNDLE)/$(PLUGIN).so  $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui
	install -m 644 $(BUNDLE)/modgui/*       $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui/

.PHONY: all clean install smoke

BUNDLE := eh_micro_synth.lv2
PLUGIN := eh_micro_synth
SO     := $(BUNDLE)/$(PLUGIN).so

CC       ?= cc
CFLAGS   ?= -O2 -fPIC -Wall -Wextra -Wno-unused-parameter
LV2_CFLAGS := $(shell pkg-config --cflags lv2)
LDFLAGS  ?= -shared -fvisibility=hidden
LDLIBS   ?= -lm

PREFIX ?= /usr/local
LV2DIR ?= $(PREFIX)/lib/lv2

all: $(SO)

$(SO): src/$(PLUGIN).c | $(BUNDLE)
	$(CC) $(CFLAGS) $(LV2_CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(BUNDLE):
	mkdir -p $@

smoke: $(SO)
	$(CC) -O2 -Wall src/smoke_test.c -ldl -lm -o /tmp/ehms_smoke
	/tmp/ehms_smoke $(SO)

clean:
	rm -f $(SO) /tmp/ehms_smoke

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m 644 $(BUNDLE)/manifest.ttl       $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	install -m 644 $(BUNDLE)/$(PLUGIN).ttl      $(DESTDIR)$(LV2DIR)/$(BUNDLE)/
	install -m 755 $(BUNDLE)/$(PLUGIN).so       $(DESTDIR)$(LV2DIR)/$(BUNDLE)/

.PHONY: all clean install smoke

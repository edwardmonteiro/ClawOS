# ClawOS Extensions
# SPDX-License-Identifier: Apache-2.0

.PHONY: all clean install examples

all: examples

examples:
	$(MAKE) -C sdk/examples/hello

clean:
	$(MAKE) -C sdk/examples/hello clean

install:
	$(MAKE) -C sdk/examples/hello install
	install -D -m 0644 sdk/include/claw_ext.h \
		$(DESTDIR)/usr/include/claw/claw_ext.h

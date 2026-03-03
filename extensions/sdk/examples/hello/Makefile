# Hello Extension Example
# SPDX-License-Identifier: Apache-2.0

CC     ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -fPIC

TARGET = hello.so

.PHONY: all clean install

all: $(TARGET)

$(TARGET): hello_ext.c
	$(CC) $(CFLAGS) -shared -I../../include -I../../../../kernel/include -o $@ $<

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -D -m 0644 $(TARGET) $(DESTDIR)/usr/lib/claw/extensions/$(TARGET)

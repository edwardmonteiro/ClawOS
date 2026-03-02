# ClawOS Init System
# SPDX-License-Identifier: Apache-2.0

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=gnu11 -static
LDFLAGS ?= -static

PREFIX  ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin

SRCDIR  = src
OBJDIR  = obj

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
TARGET  = claw-init

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)$(SBINDIR)/$(TARGET)

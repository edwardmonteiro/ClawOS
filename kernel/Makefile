# ClawOS Kernel Daemon (clawd)
# SPDX-License-Identifier: Apache-2.0

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=gnu11
LDFLAGS ?= -ldl

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/sbin

SRCDIR  = src
INCDIR  = include
OBJDIR  = obj

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
TARGET  = clawd

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

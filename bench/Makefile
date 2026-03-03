# ClawOS Benchmarks
# SPDX-License-Identifier: Apache-2.0

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=gnu11
LDFLAGS ?=

INCDIR  = ../kernel/include

TARGETS = bench_msg

.PHONY: all clean run

all: $(TARGETS)

bench_msg: bench_msg.c
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $< $(LDFLAGS)

run: $(TARGETS)
	./bench_msg

clean:
	rm -f $(TARGETS)

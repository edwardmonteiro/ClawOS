# ClawOS - The OS for Modern Agents
# Top-level Makefile
#
# SPDX-License-Identifier: Apache-2.0

CC      ?= gcc
JOBS    ?= $(shell nproc 2>/dev/null || echo 4)
DESTDIR ?=

COMPONENTS = kernel init bus openclaw cli extensions

.PHONY: all clean install image bench $(COMPONENTS)

all: $(COMPONENTS)

kernel:
	$(MAKE) -C kernel CC=$(CC)

init:
	$(MAKE) -C init CC=$(CC)

bus:
	$(MAKE) -C bus CC=$(CC)

openclaw:
	$(MAKE) -C openclaw CC=$(CC)

cli:
	$(MAKE) -C cli CC=$(CC)

extensions:
	$(MAKE) -C extensions CC=$(CC)

bench:
	$(MAKE) -C bench CC=$(CC)

bench-run: bench
	$(MAKE) -C bench run

clean:
	@for dir in $(COMPONENTS) bench; do \
		$(MAKE) -C $$dir clean 2>/dev/null; true; \
	done

install: all
	DESTDIR=$(DESTDIR) sh scripts/install.sh

image: all
	sh scripts/mkimage.sh

help:
	@echo "ClawOS Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all         Build all components"
	@echo "  kernel      Build kernel daemon (clawd)"
	@echo "  init        Build init system (claw-init)"
	@echo "  bus         Build message bus (claw-bus)"
	@echo "  openclaw    Build OpenClaw runtime"
	@echo "  cli         Build CLI tool (claw)"
	@echo "  extensions  Build extensions"
	@echo "  bench       Build benchmarks"
	@echo "  bench-run   Build and run benchmarks"
	@echo "  clean       Clean all build artifacts"
	@echo "  install     Install to DESTDIR"
	@echo "  image       Build bootable disk image"
	@echo ""
	@echo "Variables:"
	@echo "  CC=gcc      Compiler (default: gcc)"
	@echo "  DESTDIR=    Installation prefix"

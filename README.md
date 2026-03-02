# ClawOS

**The OS for Modern Agents**

A super lightweight kernel layer on top of Linux that makes OpenClaw a powerful integration engine out of the box. ClawOS provides the minimal infrastructure needed to run, orchestrate, and scale AI agents with native OS-level support.

## Architecture

```
┌─────────────────────────────────────────────┐
│                  claw CLI                   │
├─────────────────────────────────────────────┤
│            OpenClaw Runtime                 │
│   (manifests · registry · API gateway)      │
├──────────────────┬──────────────────────────┤
│    claw-bus      │      Extensions (.so)    │
│  (pub/sub IPC)   │    (dynamic plugins)     │
├──────────────────┴──────────────────────────┤
│              clawd (kernel daemon)          │
│  (agent lifecycle · IPC · sandbox · cgroups)│
├─────────────────────────────────────────────┤
│              claw-init (PID 1)              │
├─────────────────────────────────────────────┤
│              Linux Kernel                   │
└─────────────────────────────────────────────┘
```

## Components

| Component | Description |
|-----------|-------------|
| **claw-init** | Minimal init system (PID 1). Mounts filesystems, starts core services, auto-restarts on crash. |
| **clawd** | Kernel daemon. Manages agent lifecycles, cgroup sandboxing, namespace isolation, and extension loading. |
| **claw-bus** | Pub/sub message bus. High-performance inter-agent communication via Unix domain sockets and epoll. |
| **openclaw-runtime** | OpenClaw integration engine. Parses TOML manifests, deploys agents, provides API gateway. |
| **claw** | CLI tool. Status, agent management, deployment, extension control. |
| **Extension SDK** | Build plugins as `.so` shared libraries loaded dynamically by clawd. |

## Quick Start

### Build

```sh
make            # Build all components
make kernel     # Build only clawd
make install    # Install to system (use DESTDIR= for staging)
```

### Create an Agent

Write a manifest file (`my-agent.toml`):

```toml
[agent]
name = "my-agent"
version = "1.0.0"
description = "My first ClawOS agent"
exec = "/usr/lib/claw/agents/my-agent"

[resources]
memory = "256M"
cpu_shares = 100

[capabilities]
net = true
ipc = true
bus = true
openclaw = true

[lifecycle]
auto_start = true
restart_on_failure = true
max_restarts = 5

[bus]
subscribe = "events.user, events.system"

[environment]
LOG_LEVEL = "info"
```

Deploy it:

```sh
claw deploy my-agent.toml
claw status
claw agent list
```

### Build a Bootable Image

```sh
sudo make image    # Creates clawos.img (~256MB)

# Test with QEMU
qemu-system-x86_64 -m 512 -drive file=clawos.img,format=raw -nographic
```

## Extension Development

Create a plugin in C:

```c
#include <claw_ext.h>

const char *claw_ext_name(void)  { return "my-extension"; }
const char *claw_ext_version(void) { return "1.0.0"; }

int claw_ext_init(void) {
    /* Called when clawd loads the extension */
    return 0;
}

void claw_ext_cleanup(void) {
    /* Called on unload/shutdown */
}

int claw_ext_handle_msg(const struct claw_msg *msg,
                        void *reply, uint32_t *reply_len) {
    /* Handle messages directed at this extension */
    return CLAW_OK;
}
```

Build and install:

```sh
gcc -shared -fPIC -o my-extension.so my-extension.c
cp my-extension.so /usr/lib/claw/extensions/
```

## Design Principles

- **Everything is an agent** - Processes are managed as agents with declarative manifests
- **OpenClaw native** - Integration engine is a first-class citizen, not bolted on
- **Capability-based security** - Agents declare what they need; everything else is denied
- **Minimal footprint** - Alpine Linux base, musl-compatible, boots in seconds
- **Message-driven** - Agents communicate via high-performance pub/sub bus
- **Extensible** - Dynamic plugin system for adding capabilities at runtime

## Project Structure

```
ClawOS/
├── kernel/          # clawd - kernel daemon
│   ├── src/         #   main, process, ipc, sandbox, extension, log
│   └── include/     #   claw/types.h, kernel.h, ipc.h
├── init/            # claw-init - init system (PID 1)
├── bus/             # claw-bus - message bus
├── openclaw/        # OpenClaw runtime
│   ├── src/         #   runtime, manifest parser
│   └── include/     #   openclaw/runtime.h, manifest.h
├── cli/             # claw CLI tool
├── extensions/      # Extension SDK and examples
│   └── sdk/
│       ├── include/ #   claw_ext.h
│       └── examples/#   hello extension
├── config/          # Default configurations
├── scripts/         # Build, install, image creation
├── rootfs/          # Root filesystem overlay
└── Makefile         # Top-level build
```

## License

Apache License 2.0

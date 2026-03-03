# Agent Communication: Design & Rationale

## Why NOT a Network Discovery Service

The original proposal was to build:
1. A dedicated network discovery service
2. A new protocol for agent communication
3. A container sidecar for direct agent connections

After rigorous analysis of the ClawOS codebase, **all three are the wrong
solution**. Here's why, and what we built instead.

### The Misdiagnosis

The symptom looks like "agents can't discover each other," which suggests
a discovery service. But the root cause is different:

1. **The kernel already knows about every agent** — it creates them, assigns
   IDs, tracks names and state. It IS the registry.
2. **The kernel accepted messages but never forwarded them** — `handle_client()`
   logged routing intent and then dropped the message on the floor.
3. **Network namespaces broke IPC by default** — `CLONE_NEWNET` isolated agents
   from the Unix domain sockets they need for communication.
4. **Dependencies were parsed but ignored** — manifests declared `requires`
   but the deployment loop ignored ordering entirely.
5. **Request/response had no correlation** — message IDs existed but were
   never used to match responses to requests.

Building a discovery service on top of these bugs is like building GPS
navigation for a car with no engine. The car needs an engine first.

### Why Not a Sidecar

ClawOS is an **operating system**, not Kubernetes. The sidecar pattern exists
because K8s has no centralized knowledge of application semantics — you bolt
on sidecars (Envoy, Istio) to inject cross-cutting concerns.

ClawOS has `clawd`, a kernel daemon that already:
- Creates and tracks every agent
- Manages their lifecycle (start, stop, restart)
- Owns the IPC socket infrastructure
- Has name-to-ID lookup (`claw_agent_find_by_name()`)

A sidecar per agent means:
- **2x processes** — double the memory, double the socket overhead
- **2x failure domains** — sidecar crashes orphan the agent
- **Distributed consensus** — sidecars discovering each other is harder
  than just asking the kernel, which already knows everything

The right model is how real operating systems work: processes talk to the
kernel, the kernel routes. You don't run a sidecar next to every process
on Linux.

## What We Built Instead

### Architecture

```
Agent A                    clawd (kernel)                Agent B
  |                            |                            |
  |-- resolve "agent-b" ----->|                            |
  |<-- id=42 ----------------|                            |
  |                            |                            |
  |-- REQUEST dst=42 -------->|                            |
  |                            |-- forward to /ipc/42.sock->|
  |                            |                            |
  |<-- RESPONSE (via IPC) ----|<-- reply --------------------|
```

Agents communicate through the kernel or directly via IPC sockets.
The kernel is the router and the source of truth for agent identity.

### Changes Made

#### P0: Kernel Message Forwarding (`kernel/src/main.c`)

**Problem**: The kernel accepted messages and logged routing intent but
never actually forwarded them to destination agents.

**Fix**: Implemented `forward_to_agent()` — when the kernel receives a
message with a non-zero `dst` field, it resolves the destination agent's
IPC socket path (`/run/claw/ipc/{id}.sock`) and delivers the message via
a datagram socket. If the destination doesn't exist, the sender gets an
error response instead of silence.

Also implemented `send_response()` and `send_error()` — the kernel now
speaks the claw_msg protocol back to clients, preserving the request's
message ID for correlation.

#### P0: Sandbox Namespace Fix (`kernel/src/sandbox.c`)

**Problem**: `CLONE_NEWNET` (applied by default for security) prevented
agents from accessing Unix domain sockets in `/run/claw/ipc/`. Since
all IPC is socket-based, this made secure agents unable to communicate.

**Fix**: Added `setup_ipc_mount()` — before chrooting, the sandbox
bind-mounts the IPC directory, the `clawd.sock` kernel socket, and the
`bus.sock` into the agent's filesystem. This decouples two orthogonal
concerns:

- `CLAW_CAP_NET` → controls TCP/IP, raw sockets, external network
- `CLAW_CAP_IPC` → controls access to the agent communication plane

An agent can be network-isolated (no internet, no TCP) but still
communicate with other agents via Unix domain sockets. This is the
correct security boundary for an agent OS.

#### P1: Name Resolution (`kernel/src/main.c`)

**Problem**: Agents needed to know numeric agent IDs to communicate,
but had no way to discover them. IDs are assigned dynamically at
creation time.

**Fix**: Added `handle_resolve()` — a new kernel-handled request topic.
Agents send a `CLAW_MSG_REQUEST` with `topic="resolve"` and the target
agent's name as payload. The kernel responds with the agent's ID
(uint64_t) or an error if not found.

Also added `handle_list_agents()` — returns a packed array of all live
agents (ID, name, state) for debugging and tooling.

Also added `handle_agent_ready()` — agents signal readiness via
`topic="agent.ready"`, which the kernel records. This enables lifecycle
coordination.

#### P1: Dependency-Ordered Deployment (`openclaw/src/runtime.c`)

**Problem**: `openclaw_run()` deployed all auto_start agents in an
unordered loop, ignoring the `depends_on` declarations in manifests.
Agent B could start before Agent A even though B depends on A.

**Fix**: Replaced the blind loop with `deploy_in_order()` — an iterative
topological sort. Each pass deploys agents whose dependencies have all
been deployed. The loop repeats until no more progress can be made.

Handles edge cases:
- **Missing dependency manifests** — warns and deploys anyway (the
  dependency might be externally managed)
- **Circular dependencies** — detected by lack of progress; reported
  with specific missing dependency names
- **No dependencies** — deployed immediately on first pass (backward
  compatible)

#### P2: Request/Response Correlation (`kernel/src/ipc.c`)

**Problem**: `claw_ipc_request()` sent a request and waited for ANY
incoming message as the response. With multiple in-flight requests or
concurrent events, responses could be mismatched.

**Fix**: The request function now saves the message ID it generated
(`req_id`) and loops on `claw_ipc_recv()` until it gets a `RESPONSE`
message with a matching ID. Non-matching messages are discarded.
Uses `clock_gettime(CLOCK_MONOTONIC)` to track elapsed time against
the timeout so the total wait never exceeds the caller's deadline.

## What This Enables

With these fixes, the following agent interaction patterns now work:

### 1. Agent-to-Agent RPC

```c
// Agent A resolves Agent B by name, then sends a request
claw_ipc_request(&ipc, 0, "resolve", "agent-b", 8, &id, &len, 5000);
claw_ipc_request(&ipc, id, "compute", data, dlen, reply, &rlen, 10000);
```

### 2. Secure Sandboxed Communication

```toml
# Agent manifest — no network, but can still talk to other agents
[capabilities]
net = false    # no TCP/IP, no internet
ipc = true     # can reach /run/claw/ipc/ and clawd.sock
bus = true     # can pub/sub on the message bus
```

### 3. Ordered Multi-Agent Startup

```toml
# database.toml - no dependencies, starts first
[agent]
name = "database"
[lifecycle]
auto_start = true

# api-server.toml - starts after database
[agent]
name = "api-server"
[dependencies]
requires = "database"
[lifecycle]
auto_start = true
```

## Future: Multi-Host Federation

When ClawOS needs to run agents across multiple machines, the right
pattern is **kernel-to-kernel federation**, not per-agent sidecars:

```
Host 1                          Host 2
┌─────────────┐                ┌─────────────┐
│  Agent A    │                │  Agent C    │
│  Agent B    │                │  Agent D    │
│             │                │             │
│  clawd ─────── gRPC/QUIC ──── clawd      │
└─────────────┘                └─────────────┘
```

Each host's `clawd` maintains a federated agent registry. When Agent A
on Host 1 resolves "agent-c", its local `clawd` proxies the message to
Host 2's `clawd`, which delivers locally. Agents remain unaware of
topology — they always talk to their local kernel.

This follows the Plan 9 model (9P protocol) rather than the
microservices model (sidecar mesh). It's simpler, has fewer failure
modes, and is consistent with ClawOS being an OS, not an orchestrator.

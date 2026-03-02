/*
 * ClawOS - OpenClaw Manifest Parser
 *
 * Parses TOML-style agent manifest files that declare
 * agent properties, capabilities, and dependencies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCLAW_MANIFEST_H
#define OPENCLAW_MANIFEST_H

#include "runtime.h"

/*
 * Manifest file format (TOML-like):
 *
 * [agent]
 * name = "my-agent"
 * version = "1.0.0"
 * description = "My awesome agent"
 * exec = "/usr/lib/claw/agents/my-agent"
 *
 * [resources]
 * memory = "256M"
 * cpu_shares = 100
 *
 * [capabilities]
 * net = true
 * fs = false
 * ipc = true
 * openclaw = true
 *
 * [lifecycle]
 * auto_start = true
 * restart_on_failure = true
 * max_restarts = 5
 *
 * [dependencies]
 * requires = ["other-agent", "database-agent"]
 *
 * [bus]
 * subscribe = ["events.user", "events.system"]
 *
 * [environment]
 * AGENT_MODE = "production"
 * LOG_LEVEL = "info"
 */

/* Parse a manifest file into a manifest struct */
int  openclaw_manifest_parse(const char *path, struct openclaw_manifest *m);

/* Validate a parsed manifest */
int  openclaw_manifest_validate(const struct openclaw_manifest *m);

/* Serialize a manifest to a string (for API responses) */
int  openclaw_manifest_to_string(const struct openclaw_manifest *m,
                                 char *buf, size_t buflen);

#endif /* OPENCLAW_MANIFEST_H */

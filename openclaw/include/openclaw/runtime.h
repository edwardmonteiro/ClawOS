/*
 * ClawOS - OpenClaw Runtime Interface
 *
 * The OpenClaw runtime provides the integration engine that
 * makes ClawOS a powerful platform for agent orchestration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCLAW_RUNTIME_H
#define OPENCLAW_RUNTIME_H

#include "../../kernel/include/claw/types.h"

#define OPENCLAW_VERSION       "0.1.0"
#define OPENCLAW_SOCKET        "/run/claw/openclaw.sock"
#define OPENCLAW_CONFIG        "/etc/claw/openclaw.conf"
#define OPENCLAW_MANIFESTS_DIR "/etc/claw/manifests"
#define OPENCLAW_REGISTRY_DIR  "/var/lib/claw/registry"

/* Agent manifest - declarative agent definition */
struct openclaw_manifest {
    char        name[CLAW_MAX_NAME];
    char        version[32];
    char        description[256];
    char        exec_path[CLAW_MAX_PATH];
    claw_caps_t caps;
    uint64_t    memory_limit;
    uint64_t    cpu_shares;
    int         auto_start;
    int         restart_on_failure;
    int         max_restarts;
    char        depends_on[8][CLAW_MAX_NAME];
    int         depends_count;
    char        topics[16][CLAW_MAX_NAME];    /* bus topics to subscribe */
    int         topic_count;
    char        env[32][CLAW_MAX_PATH];       /* environment variables */
    int         env_count;
};

/* Runtime context */
struct openclaw_runtime {
    int                     running;
    int                     sock_fd;
    int                     epoll_fd;
    struct openclaw_manifest manifests[CLAW_MAX_AGENTS];
    int                     manifest_count;
    char                    registry_dir[CLAW_MAX_PATH];
};

/* Runtime lifecycle */
int  openclaw_init(struct openclaw_runtime *rt, const char *config);
int  openclaw_run(struct openclaw_runtime *rt);
void openclaw_shutdown(struct openclaw_runtime *rt);

/* Manifest operations */
int  openclaw_load_manifest(struct openclaw_runtime *rt, const char *path);
int  openclaw_load_manifests_dir(struct openclaw_runtime *rt, const char *dir);
int  openclaw_deploy_manifest(struct openclaw_runtime *rt,
                              const struct openclaw_manifest *m);

/* API operations */
int  openclaw_api_list_agents(struct openclaw_runtime *rt,
                              char *buf, size_t buflen);
int  openclaw_api_deploy(struct openclaw_runtime *rt,
                         const char *manifest_json);
int  openclaw_api_status(struct openclaw_runtime *rt,
                         char *buf, size_t buflen);

#endif /* OPENCLAW_RUNTIME_H */

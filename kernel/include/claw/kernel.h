/*
 * ClawOS - Kernel Interface
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLAW_KERNEL_H
#define CLAW_KERNEL_H

#include "types.h"

#define CLAWD_VERSION_MAJOR  0
#define CLAWD_VERSION_MINOR  1
#define CLAWD_VERSION_PATCH  0
#define CLAWD_VERSION_STRING "0.1.0"

#define CLAWD_SOCKET_PATH    "/run/claw/clawd.sock"
#define CLAWD_PID_FILE       "/run/claw/clawd.pid"
#define CLAWD_CONFIG_PATH    "/etc/claw/clawd.conf"
#define CLAWD_LOG_PATH       "/var/log/claw/clawd.log"
#define CLAWD_EXT_DIR        "/usr/lib/claw/extensions"
#define CLAWD_CGROUP_ROOT    "/sys/fs/cgroup/claw"

#define CLAW_MAX_CLIENTS     256   /* max concurrent kernel connections */

/* Kernel context */
struct claw_kernel {
    int                     running;
    int                     sock_fd;
    int                     epoll_fd;
    int                     fwd_fd;          /* cached DGRAM socket for forwarding */
    struct claw_agent       agents[CLAW_MAX_AGENTS];
    int                     agent_count;
    struct claw_extension   extensions[CLAW_MAX_EXTENSIONS];
    int                     ext_count;
    char                    config_path[CLAW_MAX_PATH];
    int                     log_level;
};

/* Kernel lifecycle */
int  claw_kernel_init(struct claw_kernel *k, const char *config);
int  claw_kernel_run(struct claw_kernel *k);
void claw_kernel_shutdown(struct claw_kernel *k);

/* Agent management */
claw_aid_t claw_agent_create(struct claw_kernel *k, const char *name,
                             claw_caps_t caps);
int  claw_agent_start(struct claw_kernel *k, claw_aid_t id);
int  claw_agent_stop(struct claw_kernel *k, claw_aid_t id);
int  claw_agent_destroy(struct claw_kernel *k, claw_aid_t id);
struct claw_agent *claw_agent_find(struct claw_kernel *k, claw_aid_t id);
struct claw_agent *claw_agent_find_by_name(struct claw_kernel *k,
                                           const char *name);

/* Extension management */
int  claw_ext_load(struct claw_kernel *k, const char *path);
int  claw_ext_unload(struct claw_kernel *k, claw_eid_t id);
int  claw_ext_load_dir(struct claw_kernel *k, const char *dir);

/* Logging */
enum claw_log_level {
    CLAW_LOG_ERROR = 0,
    CLAW_LOG_WARN  = 1,
    CLAW_LOG_INFO  = 2,
    CLAW_LOG_DEBUG = 3,
};

void claw_log(int level, const char *fmt, ...);

#endif /* CLAW_KERNEL_H */

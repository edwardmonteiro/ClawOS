/*
 * ClawOS - Core Type Definitions
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLAW_TYPES_H
#define CLAW_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Agent ID - unique identifier for each agent/process */
typedef uint64_t claw_aid_t;

/* Message ID */
typedef uint64_t claw_mid_t;

/* Extension ID */
typedef uint32_t claw_eid_t;

/* Capabilities bitmask */
typedef uint64_t claw_caps_t;

/* Agent states */
enum claw_agent_state {
    CLAW_AGENT_CREATED   = 0,
    CLAW_AGENT_READY     = 1,
    CLAW_AGENT_RUNNING   = 2,
    CLAW_AGENT_WAITING   = 3,
    CLAW_AGENT_STOPPED   = 4,
    CLAW_AGENT_DEAD      = 5,
};

/* IPC message types */
enum claw_msg_type {
    CLAW_MSG_REQUEST     = 0,
    CLAW_MSG_RESPONSE    = 1,
    CLAW_MSG_EVENT       = 2,
    CLAW_MSG_SIGNAL      = 3,
    CLAW_MSG_STREAM      = 4,
};

/* Agent capabilities */
#define CLAW_CAP_NET         (1ULL << 0)   /* Network access */
#define CLAW_CAP_FS          (1ULL << 1)   /* Filesystem access */
#define CLAW_CAP_PROC        (1ULL << 2)   /* Process management */
#define CLAW_CAP_IPC         (1ULL << 3)   /* Inter-process communication */
#define CLAW_CAP_HW          (1ULL << 4)   /* Hardware access */
#define CLAW_CAP_EXT         (1ULL << 5)   /* Extension loading */
#define CLAW_CAP_OPENCLAW    (1ULL << 6)   /* OpenClaw runtime */
#define CLAW_CAP_ADMIN       (1ULL << 7)   /* System administration */
#define CLAW_CAP_BUS         (1ULL << 8)   /* Bus publish/subscribe */
#define CLAW_CAP_SANDBOX     (1ULL << 9)   /* Sandbox management */
#define CLAW_CAP_ALL         0xFFFFFFFFFFFFFFFFULL

/* Maximum limits */
#define CLAW_MAX_NAME        64
#define CLAW_MAX_PATH        256
#define CLAW_MAX_MSG_SIZE    (64 * 1024)   /* 64KB message limit */
#define CLAW_MAX_AGENTS      4096
#define CLAW_MAX_EXTENSIONS  256
#define CLAW_MAX_TOPICS      1024

/* Agent descriptor */
struct claw_agent {
    claw_aid_t          id;
    char                name[CLAW_MAX_NAME];
    enum claw_agent_state state;
    claw_caps_t         caps;
    pid_t               pid;
    int                 priority;
    uint64_t            memory_limit;    /* bytes, 0 = unlimited */
    uint64_t            cpu_shares;      /* cgroup cpu shares */
    char                cgroup[CLAW_MAX_PATH];
    char                rootfs[CLAW_MAX_PATH];
};

/* IPC message header */
struct claw_msg {
    claw_mid_t          id;
    enum claw_msg_type  type;
    claw_aid_t          src;
    claw_aid_t          dst;
    char                topic[CLAW_MAX_NAME];
    uint32_t            len;
    uint8_t             data[];          /* flexible array member */
};

/* Extension descriptor */
struct claw_extension {
    claw_eid_t          id;
    char                name[CLAW_MAX_NAME];
    char                path[CLAW_MAX_PATH];
    void               *handle;          /* dlopen handle */
    int                 (*init)(void);
    void                (*cleanup)(void);
    int                 loaded;
};

/* Return codes */
#define CLAW_OK           0
#define CLAW_ERR         -1
#define CLAW_ERR_NOMEM   -2
#define CLAW_ERR_PERM    -3
#define CLAW_ERR_NOENT   -4
#define CLAW_ERR_BUSY    -5
#define CLAW_ERR_INVAL   -6
#define CLAW_ERR_FULL    -7
#define CLAW_ERR_IO      -8

#endif /* CLAW_TYPES_H */

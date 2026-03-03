/*
 * ClawOS - IPC Interface
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLAW_IPC_H
#define CLAW_IPC_H

#include "types.h"

#define CLAW_IPC_SOCKET_DIR  "/run/claw/ipc"

/* IPC context for an agent */
struct claw_ipc {
    int         fd;
    claw_aid_t  self_id;
    char        socket_path[CLAW_MAX_PATH];
};

/* IPC operations */
int  claw_ipc_init(struct claw_ipc *ipc, claw_aid_t id);
void claw_ipc_cleanup(struct claw_ipc *ipc);
int  claw_ipc_send(struct claw_ipc *ipc, const struct claw_msg *msg);
int  claw_ipc_recv(struct claw_ipc *ipc, struct claw_msg *msg, int timeout_ms);
int  claw_ipc_connect(struct claw_ipc *ipc, const char *target);

/* Convenience helpers */
int  claw_ipc_request(struct claw_ipc *ipc, claw_aid_t dst,
                      const char *topic, const void *data, uint32_t len,
                      void *reply, uint32_t *reply_len, int timeout_ms);
int  claw_ipc_emit(struct claw_ipc *ipc, const char *topic,
                   const void *data, uint32_t len);

#endif /* CLAW_IPC_H */

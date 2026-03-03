/*
 * ClawOS - Inter-Process Communication
 *
 * Unix domain socket based IPC for agent-to-agent and
 * agent-to-kernel communication. Designed for low latency
 * and zero-copy where possible.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include "claw/ipc.h"
#include "claw/kernel.h"

static claw_mid_t next_mid = 1;

int claw_ipc_init(struct claw_ipc *ipc, claw_aid_t id)
{
    struct sockaddr_un addr;

    memset(ipc, 0, sizeof(*ipc));
    ipc->self_id = id;

    snprintf(ipc->socket_path, sizeof(ipc->socket_path),
             "%s/%lu.sock", CLAW_IPC_SOCKET_DIR, id);

    ipc->fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (ipc->fd < 0)
        return CLAW_ERR_IO;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);

    unlink(ipc->socket_path);
    if (bind(ipc->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ipc->fd);
        ipc->fd = -1;
        return CLAW_ERR_IO;
    }

    return CLAW_OK;
}

void claw_ipc_cleanup(struct claw_ipc *ipc)
{
    if (ipc->fd >= 0) {
        close(ipc->fd);
        ipc->fd = -1;
    }
    if (ipc->socket_path[0])
        unlink(ipc->socket_path);
}

int claw_ipc_send(struct claw_ipc *ipc, const struct claw_msg *msg)
{
    struct sockaddr_un dst_addr;
    char dst_path[CLAW_MAX_PATH];
    ssize_t n;
    size_t total = sizeof(struct claw_msg) + msg->len;

    /* Resolve destination socket path */
    snprintf(dst_path, sizeof(dst_path),
             "%s/%lu.sock", CLAW_IPC_SOCKET_DIR, msg->dst);

    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sun_family = AF_UNIX;
    strncpy(dst_addr.sun_path, dst_path, sizeof(dst_addr.sun_path) - 1);

    n = sendto(ipc->fd, msg, total, 0,
               (struct sockaddr *)&dst_addr, sizeof(dst_addr));
    if (n < 0)
        return CLAW_ERR_IO;

    return CLAW_OK;
}

int claw_ipc_recv(struct claw_ipc *ipc, struct claw_msg *msg, int timeout_ms)
{
    struct pollfd pfd;
    ssize_t n;

    pfd.fd = ipc->fd;
    pfd.events = POLLIN;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0)
        return CLAW_ERR_IO;
    if (rc == 0)
        return CLAW_ERR_BUSY;  /* timeout */

    n = recv(ipc->fd, msg, CLAW_MAX_MSG_SIZE, 0);
    if (n < 0)
        return CLAW_ERR_IO;
    if ((size_t)n < sizeof(struct claw_msg))
        return CLAW_ERR_INVAL;

    return CLAW_OK;
}

int claw_ipc_connect(struct claw_ipc *ipc, const char *target)
{
    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, target, sizeof(addr.sun_path) - 1);

    if (connect(ipc->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return CLAW_ERR_IO;

    return CLAW_OK;
}

int claw_ipc_request(struct claw_ipc *ipc, claw_aid_t dst,
                     const char *topic, const void *data, uint32_t len,
                     void *reply, uint32_t *reply_len, int timeout_ms)
{
    /* Allocate message with data */
    size_t msg_size = sizeof(struct claw_msg) + len;
    struct claw_msg *msg = malloc(msg_size);
    if (!msg)
        return CLAW_ERR_NOMEM;

    memset(msg, 0, sizeof(struct claw_msg));
    claw_mid_t req_id = __sync_fetch_and_add(&next_mid, 1);
    msg->id = req_id;
    msg->type = CLAW_MSG_REQUEST;
    msg->src = ipc->self_id;
    msg->dst = dst;
    strncpy(msg->topic, topic, CLAW_MAX_NAME - 1);
    msg->len = len;
    if (data && len > 0)
        memcpy(msg->data, data, len);

    int rc = claw_ipc_send(ipc, msg);
    free(msg);
    if (rc != CLAW_OK)
        return rc;

    /*
     * Wait for a response that matches our request ID.
     * This prevents mismatched responses when multiple requests
     * are in flight. Non-matching messages are discarded -
     * in practice, agents should use separate IPC contexts for
     * concurrent request/response and event listening.
     */
    struct claw_msg *resp = malloc(CLAW_MAX_MSG_SIZE);
    if (!resp)
        return CLAW_ERR_NOMEM;

    int remaining_ms = timeout_ms;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (remaining_ms > 0) {
        rc = claw_ipc_recv(ipc, resp, remaining_ms);
        if (rc != CLAW_OK)
            break;

        /* Check if this response matches our request */
        if (resp->type == CLAW_MSG_RESPONSE && resp->id == req_id) {
            if (reply && reply_len) {
                uint32_t copy_len = resp->len < *reply_len
                                    ? resp->len : *reply_len;
                memcpy(reply, resp->data, copy_len);
                *reply_len = copy_len;
            }
            free(resp);
            return CLAW_OK;
        }

        /* Not our response - update remaining time and retry */
        clock_gettime(CLOCK_MONOTONIC, &now);
        int elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                         (now.tv_nsec - start.tv_nsec) / 1000000;
        remaining_ms = timeout_ms - elapsed_ms;
    }

    free(resp);
    return rc == CLAW_OK ? CLAW_ERR_BUSY : rc; /* timeout */
}

int claw_ipc_emit(struct claw_ipc *ipc, const char *topic,
                  const void *data, uint32_t len)
{
    size_t msg_size = sizeof(struct claw_msg) + len;
    struct claw_msg *msg = malloc(msg_size);
    if (!msg)
        return CLAW_ERR_NOMEM;

    memset(msg, 0, sizeof(struct claw_msg));
    msg->id = __sync_fetch_and_add(&next_mid, 1);
    msg->type = CLAW_MSG_EVENT;
    msg->src = ipc->self_id;
    msg->dst = 0;  /* broadcast */
    strncpy(msg->topic, topic, CLAW_MAX_NAME - 1);
    msg->len = len;
    if (data && len > 0)
        memcpy(msg->data, data, len);

    int rc = claw_ipc_send(ipc, msg);
    free(msg);
    return rc;
}

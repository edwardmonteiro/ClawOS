/*
 * ClawOS - Message Bus Interface
 *
 * Pub/sub message bus for inter-agent communication.
 * Agents subscribe to topics and receive messages
 * published to those topics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLAW_BUS_H
#define CLAW_BUS_H

#include "../../kernel/include/claw/types.h"

#define CLAW_BUS_SOCKET    "/run/claw/bus.sock"
#define CLAW_BUS_CONFIG    "/etc/claw/bus.conf"

/* Subscription */
struct claw_subscription {
    claw_aid_t  agent_id;
    char        topic[CLAW_MAX_NAME];
    int         fd;       /* client socket fd */
    int         active;
};

/* Bus context */
struct claw_bus {
    int                      running;
    int                      sock_fd;
    int                      epoll_fd;
    struct claw_subscription subs[CLAW_MAX_TOPICS];
    int                      sub_count;
};

/* Bus daemon operations */
int  claw_bus_init(struct claw_bus *bus);
int  claw_bus_run(struct claw_bus *bus);
void claw_bus_shutdown(struct claw_bus *bus);

/* Client API */
int  claw_bus_connect(void);
int  claw_bus_subscribe(int bus_fd, const char *topic, claw_aid_t agent_id);
int  claw_bus_unsubscribe(int bus_fd, const char *topic, claw_aid_t agent_id);
int  claw_bus_publish(int bus_fd, const char *topic,
                      const void *data, uint32_t len);

#endif /* CLAW_BUS_H */

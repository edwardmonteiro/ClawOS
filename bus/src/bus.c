/*
 * claw-bus - ClawOS Message Bus
 *
 * High-performance pub/sub message bus for agent communication.
 * Uses Unix domain sockets and epoll for low-latency message delivery.
 *
 * Protocol:
 *   - Subscribe:   MSG_REQUEST with topic="bus.subscribe", data=topic_name
 *   - Unsubscribe: MSG_REQUEST with topic="bus.unsubscribe", data=topic_name
 *   - Publish:     MSG_EVENT with topic=user_topic, data=payload
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>

#include "claw/bus.h"

/* Forward declaration for logging */
extern void claw_log(int level, const char *fmt, ...);
enum { LOG_ERROR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DEBUG = 3 };

static volatile int got_signal = 0;

static void signal_handler(int sig)
{
    (void)sig;
    got_signal = 1;
}

/*
 * FNV-1a hash for topic strings.
 * Simple, fast, good distribution for short strings.
 */
static unsigned int topic_hash(const char *s)
{
    unsigned int h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h & (CLAW_BUS_HASH_BUCKETS - 1);
}

int claw_bus_init(struct claw_bus *bus)
{
    struct sockaddr_un addr;

    memset(bus, 0, sizeof(*bus));

    /* Initialize hash chains to empty (-1) */
    for (int i = 0; i < CLAW_BUS_HASH_BUCKETS; i++)
        bus->topic_heads[i] = -1;
    bus->wildcard_head = -1;

    bus->sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (bus->sock_fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLAW_BUS_SOCKET, sizeof(addr.sun_path) - 1);

    unlink(CLAW_BUS_SOCKET);
    if (bind(bus->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(bus->sock_fd);
        return -1;
    }

    if (listen(bus->sock_fd, 64) < 0) {
        close(bus->sock_fd);
        return -1;
    }

    bus->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (bus->epoll_fd < 0) {
        close(bus->sock_fd);
        return -1;
    }

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = bus->sock_fd };
    epoll_ctl(bus->epoll_fd, EPOLL_CTL_ADD, bus->sock_fd, &ev);

    fprintf(stderr, "[claw-bus] initialized on %s\n", CLAW_BUS_SOCKET);
    return 0;
}

static int add_subscription(struct claw_bus *bus, const char *topic,
                           claw_aid_t agent_id, int fd)
{
    if (bus->sub_count >= CLAW_MAX_TOPICS)
        return -1;

    int idx = bus->sub_count;
    struct claw_subscription *sub = &bus->subs[idx];
    sub->agent_id = agent_id;
    strncpy(sub->topic, topic, CLAW_MAX_NAME - 1);
    sub->fd = fd;
    sub->active = 1;

    /* Insert into hash chain */
    if (topic[0] == '\0') {
        sub->next = bus->wildcard_head;
        bus->wildcard_head = idx;
    } else {
        unsigned int h = topic_hash(topic);
        sub->next = bus->topic_heads[h];
        bus->topic_heads[h] = idx;
    }

    bus->sub_count++;
    fprintf(stderr, "[claw-bus] agent %lu subscribed to '%s'\n",
            agent_id, topic);
    return 0;
}

static void remove_subscription(struct claw_bus *bus, const char *topic,
                               claw_aid_t agent_id)
{
    /* Mark inactive. We don't remove from the hash chain —
       inactive entries are skipped during delivery. Chains are
       compacted lazily or on next full rebuild if needed. */
    unsigned int h = topic_hash(topic);
    int idx = bus->topic_heads[h];
    while (idx >= 0) {
        if (bus->subs[idx].agent_id == agent_id &&
            strcmp(bus->subs[idx].topic, topic) == 0) {
            bus->subs[idx].active = 0;
            fprintf(stderr, "[claw-bus] agent %lu unsubscribed from '%s'\n",
                    agent_id, topic);
            return;
        }
        idx = bus->subs[idx].next;
    }
}

/*
 * Deliver a message to a chain of subscribers.
 * Walks the linked list starting at `head`, sending to each
 * active subscriber that isn't the sender.
 */
static void deliver_chain(struct claw_bus *bus, int head,
                          const struct claw_msg *msg, size_t total)
{
    int idx = head;
    while (idx >= 0) {
        struct claw_subscription *sub = &bus->subs[idx];
        int next = sub->next;

        if (sub->active && sub->agent_id != msg->src) {
            ssize_t n = send(sub->fd, msg, total, MSG_NOSIGNAL);
            if (n < 0)
                sub->active = 0;
        }

        idx = next;
    }
}

/*
 * Deliver a published message to matching subscribers.
 *
 * Instead of scanning all 1024 subscription slots, this uses the
 * topic hash index to jump directly to the chain of subscribers
 * for the published topic. Wildcard subscribers (empty topic) are
 * delivered separately.
 *
 * Cost: O(matching_subs + wildcard_subs) instead of O(all_subs).
 */
static void deliver_to_subscribers(struct claw_bus *bus,
                                  const struct claw_msg *msg)
{
    size_t total = sizeof(struct claw_msg) + msg->len;

    /* Deliver to topic-specific subscribers */
    if (msg->topic[0] != '\0') {
        unsigned int h = topic_hash(msg->topic);
        int idx = bus->topic_heads[h];
        while (idx >= 0) {
            struct claw_subscription *sub = &bus->subs[idx];
            int next = sub->next;

            /* Hash collision: verify exact topic match */
            if (sub->active && sub->agent_id != msg->src &&
                strcmp(sub->topic, msg->topic) == 0) {
                ssize_t n = send(sub->fd, msg, total, MSG_NOSIGNAL);
                if (n < 0)
                    sub->active = 0;
            }

            idx = next;
        }
    }

    /* Deliver to wildcard subscribers (empty topic = match all) */
    deliver_chain(bus, bus->wildcard_head, msg, total);
}

static void handle_bus_client(struct claw_bus *bus, int client_fd)
{
    char buf[CLAW_MAX_MSG_SIZE];
    ssize_t n;

    n = read(client_fd, buf, sizeof(buf));
    if (n <= 0)
        return;

    if ((size_t)n < sizeof(struct claw_msg))
        return;

    struct claw_msg *msg = (struct claw_msg *)buf;

    if (msg->type == CLAW_MSG_REQUEST) {
        if (strcmp(msg->topic, "bus.subscribe") == 0 && msg->len > 0) {
            char topic[CLAW_MAX_NAME] = {0};
            size_t copy = msg->len < CLAW_MAX_NAME - 1 ? msg->len : CLAW_MAX_NAME - 1;
            memcpy(topic, msg->data, copy);
            add_subscription(bus, topic, msg->src, client_fd);
        } else if (strcmp(msg->topic, "bus.unsubscribe") == 0 && msg->len > 0) {
            char topic[CLAW_MAX_NAME] = {0};
            size_t copy = msg->len < CLAW_MAX_NAME - 1 ? msg->len : CLAW_MAX_NAME - 1;
            memcpy(topic, msg->data, copy);
            remove_subscription(bus, topic, msg->src);
        }
    } else if (msg->type == CLAW_MSG_EVENT) {
        deliver_to_subscribers(bus, msg);
    }
}

int claw_bus_run(struct claw_bus *bus)
{
    struct epoll_event events[64];

    bus->running = 1;
    fprintf(stderr, "[claw-bus] running\n");

    while (bus->running && !got_signal) {
        int nfds = epoll_wait(bus->epoll_fd, events, 64, 1000);
        if (nfds < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == bus->sock_fd) {
                int client = accept4(bus->sock_fd, NULL, NULL, SOCK_CLOEXEC);
                if (client >= 0) {
                    struct epoll_event ev = { .events = EPOLLIN, .data.fd = client };
                    epoll_ctl(bus->epoll_fd, EPOLL_CTL_ADD, client, &ev);
                }
            } else {
                handle_bus_client(bus, events[i].data.fd);
                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    epoll_ctl(bus->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                }
            }
        }
    }

    return 0;
}

void claw_bus_shutdown(struct claw_bus *bus)
{
    fprintf(stderr, "[claw-bus] shutting down\n");

    /* Close all subscriber connections */
    for (int i = 0; i < bus->sub_count; i++) {
        if (bus->subs[i].active && bus->subs[i].fd >= 0)
            close(bus->subs[i].fd);
    }

    if (bus->epoll_fd >= 0)
        close(bus->epoll_fd);
    if (bus->sock_fd >= 0)
        close(bus->sock_fd);

    unlink(CLAW_BUS_SOCKET);
}

/* Client-side API */

int claw_bus_connect(void)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLAW_BUS_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * Stack-allocated bus message construction.
 * Topic names are always < 64 bytes, so subscribe/unsubscribe
 * never needs heap allocation. Publish uses the stack for
 * payloads up to 4KB.
 */
#define CLAW_BUS_STACK_MAX  4096

int claw_bus_subscribe(int bus_fd, const char *topic, claw_aid_t agent_id)
{
    size_t topic_len = strlen(topic);
    size_t msg_size = sizeof(struct claw_msg) + topic_len + 1;
    char buf[sizeof(struct claw_msg) + CLAW_MAX_NAME];
    struct claw_msg *msg = (struct claw_msg *)buf;

    memset(msg, 0, sizeof(struct claw_msg));
    msg->type = CLAW_MSG_REQUEST;
    msg->src = agent_id;
    strncpy(msg->topic, "bus.subscribe", CLAW_MAX_NAME - 1);
    msg->len = topic_len + 1;
    memcpy(msg->data, topic, topic_len);
    msg->data[topic_len] = '\0';

    ssize_t n = write(bus_fd, msg, msg_size);
    return n > 0 ? 0 : -1;
}

int claw_bus_unsubscribe(int bus_fd, const char *topic, claw_aid_t agent_id)
{
    size_t topic_len = strlen(topic);
    size_t msg_size = sizeof(struct claw_msg) + topic_len + 1;
    char buf[sizeof(struct claw_msg) + CLAW_MAX_NAME];
    struct claw_msg *msg = (struct claw_msg *)buf;

    memset(msg, 0, sizeof(struct claw_msg));
    msg->type = CLAW_MSG_REQUEST;
    msg->src = agent_id;
    strncpy(msg->topic, "bus.unsubscribe", CLAW_MAX_NAME - 1);
    msg->len = topic_len + 1;
    memcpy(msg->data, topic, topic_len);
    msg->data[topic_len] = '\0';

    ssize_t n = write(bus_fd, msg, msg_size);
    return n > 0 ? 0 : -1;
}

int claw_bus_publish(int bus_fd, const char *topic,
                     const void *data, uint32_t len)
{
    size_t msg_size = sizeof(struct claw_msg) + len;
    char stack_buf[sizeof(struct claw_msg) + CLAW_BUS_STACK_MAX];
    struct claw_msg *msg;
    int heap = 0;

    if (len <= CLAW_BUS_STACK_MAX) {
        msg = (struct claw_msg *)stack_buf;
    } else {
        msg = calloc(1, msg_size);
        if (!msg) return -1;
        heap = 1;
    }

    memset(msg, 0, sizeof(struct claw_msg));
    msg->type = CLAW_MSG_EVENT;
    strncpy(msg->topic, topic, CLAW_MAX_NAME - 1);
    msg->len = len;
    if (data && len > 0)
        memcpy(msg->data, data, len);

    ssize_t n = write(bus_fd, msg, msg_size);
    if (heap)
        free(msg);
    return n > 0 ? 0 : -1;
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    struct claw_bus bus;
    int foreground = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0)
            foreground = 1;
    }

    if (!foreground) {
        pid_t pid = fork();
        if (pid < 0) return 1;
        if (pid > 0) return 0;
        setsid();
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    if (claw_bus_init(&bus) < 0) {
        fprintf(stderr, "claw-bus: init failed\n");
        return 1;
    }

    claw_bus_run(&bus);
    claw_bus_shutdown(&bus);

    return 0;
}

/*
 * openclaw-runtime - OpenClaw Runtime Engine
 *
 * The core integration engine that manages agent manifests,
 * handles deployment, and provides the API gateway for
 * external orchestration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>

#include "openclaw/runtime.h"
#include "openclaw/manifest.h"

static volatile int got_signal = 0;

static void signal_handler(int sig)
{
    (void)sig;
    got_signal = 1;
}

static void rt_log(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[openclaw] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

int openclaw_init(struct openclaw_runtime *rt, const char *config)
{
    struct sockaddr_un addr;

    memset(rt, 0, sizeof(*rt));
    strncpy(rt->registry_dir, OPENCLAW_REGISTRY_DIR,
            sizeof(rt->registry_dir) - 1);

    /* Create registry directory */
    mkdir(rt->registry_dir, 0755);

    /* Create API socket */
    rt->sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (rt->sock_fd < 0) {
        rt_log("socket failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OPENCLAW_SOCKET, sizeof(addr.sun_path) - 1);

    unlink(OPENCLAW_SOCKET);
    if (bind(rt->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        rt_log("bind failed: %s", strerror(errno));
        close(rt->sock_fd);
        return -1;
    }

    if (listen(rt->sock_fd, 32) < 0) {
        close(rt->sock_fd);
        return -1;
    }

    /* Epoll for async I/O */
    rt->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (rt->epoll_fd < 0) {
        close(rt->sock_fd);
        return -1;
    }

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = rt->sock_fd };
    epoll_ctl(rt->epoll_fd, EPOLL_CTL_ADD, rt->sock_fd, &ev);

    (void)config;
    rt_log("v%s initialized", OPENCLAW_VERSION);
    return 0;
}

int openclaw_deploy_manifest(struct openclaw_runtime *rt,
                             const struct openclaw_manifest *m)
{
    rt_log("deploying agent: %s v%s", m->name, m->version);

    /* Connect to clawd and create agent */
    int clawd_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (clawd_fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/run/claw/clawd.sock", sizeof(addr.sun_path) - 1);

    if (connect(clawd_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        rt_log("cannot connect to clawd: %s", strerror(errno));
        close(clawd_fd);
        return -1;
    }

    /* Send create agent request via IPC */
    struct claw_msg *msg = calloc(1, sizeof(struct claw_msg) + sizeof(*m));
    if (!msg) {
        close(clawd_fd);
        return -1;
    }

    msg->type = CLAW_MSG_REQUEST;
    strncpy(msg->topic, "agent.create", CLAW_MAX_NAME - 1);
    msg->len = sizeof(*m);
    memcpy(msg->data, m, sizeof(*m));

    write(clawd_fd, msg, sizeof(struct claw_msg) + msg->len);
    free(msg);
    close(clawd_fd);

    /* Subscribe to bus topics */
    if (m->topic_count > 0) {
        int bus_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (bus_fd >= 0) {
            struct sockaddr_un bus_addr;
            memset(&bus_addr, 0, sizeof(bus_addr));
            bus_addr.sun_family = AF_UNIX;
            strncpy(bus_addr.sun_path, "/run/claw/bus.sock",
                    sizeof(bus_addr.sun_path) - 1);

            if (connect(bus_fd, (struct sockaddr *)&bus_addr,
                        sizeof(bus_addr)) == 0) {
                for (int i = 0; i < m->topic_count; i++) {
                    rt_log("subscribing %s to topic: %s",
                           m->name, m->topics[i]);
                }
            }
            close(bus_fd);
        }
    }

    rt_log("agent deployed: %s", m->name);
    return 0;
}

/* Simple JSON-ish response builders */

int openclaw_api_list_agents(struct openclaw_runtime *rt,
                             char *buf, size_t buflen)
{
    int off = 0;
    off += snprintf(buf + off, buflen - off, "{\"agents\":[");

    for (int i = 0; i < rt->manifest_count; i++) {
        if (i > 0)
            off += snprintf(buf + off, buflen - off, ",");
        off += snprintf(buf + off, buflen - off,
                        "{\"name\":\"%s\",\"version\":\"%s\","
                        "\"auto_start\":%s}",
                        rt->manifests[i].name,
                        rt->manifests[i].version,
                        rt->manifests[i].auto_start ? "true" : "false");
    }

    off += snprintf(buf + off, buflen - off, "]}");
    return off;
}

int openclaw_api_status(struct openclaw_runtime *rt,
                        char *buf, size_t buflen)
{
    return snprintf(buf, buflen,
                    "{\"version\":\"%s\",\"agents\":%d,\"status\":\"running\"}",
                    OPENCLAW_VERSION, rt->manifest_count);
}

static void handle_api_request(struct openclaw_runtime *rt, int client_fd)
{
    char req[4096];
    char resp[8192];
    ssize_t n;

    n = read(client_fd, req, sizeof(req) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    /* Simple command dispatch */
    if (strncmp(req, "LIST", 4) == 0) {
        openclaw_api_list_agents(rt, resp, sizeof(resp));
    } else if (strncmp(req, "STATUS", 6) == 0) {
        openclaw_api_status(rt, resp, sizeof(resp));
    } else if (strncmp(req, "DEPLOY ", 7) == 0) {
        /* Deploy from manifest path */
        char *path = req + 7;
        char *nl = strchr(path, '\n');
        if (nl) *nl = '\0';

        struct openclaw_manifest m;
        if (openclaw_manifest_parse(path, &m) == 0) {
            openclaw_deploy_manifest(rt, &m);
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"deployed\",\"agent\":\"%s\"}", m.name);
        } else {
            snprintf(resp, sizeof(resp),
                     "{\"error\":\"invalid manifest\"}");
        }
    } else {
        snprintf(resp, sizeof(resp), "{\"error\":\"unknown command\"}");
    }

    write(client_fd, resp, strlen(resp));
    close(client_fd);
}

/*
 * Check if a named agent has been deployed already.
 */
static int is_deployed(const int *deployed, int count,
                       struct openclaw_runtime *rt, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(rt->manifests[deployed[i]].name, name) == 0)
            return 1;
    }
    return 0;
}

/*
 * Find manifest index by name. Returns -1 if not found.
 */
static int find_manifest(struct openclaw_runtime *rt, const char *name)
{
    for (int i = 0; i < rt->manifest_count; i++) {
        if (strcmp(rt->manifests[i].name, name) == 0)
            return i;
    }
    return -1;
}

/*
 * Deploy auto_start agents in dependency order.
 *
 * Strategy: iterative resolution. On each pass, deploy agents whose
 * dependencies have all been deployed. Repeat until no progress is
 * made (cycle or missing dependency).
 *
 * This is simple topological sort without requiring a graph library.
 * For the expected number of agents (<100), O(n^2) is fine.
 */
static void deploy_in_order(struct openclaw_runtime *rt)
{
    int deployed[CLAW_MAX_AGENTS];
    int deployed_count = 0;
    int total_auto = 0;

    for (int i = 0; i < rt->manifest_count; i++) {
        if (rt->manifests[i].auto_start)
            total_auto++;
    }

    if (total_auto == 0)
        return;

    int progress = 1;
    while (progress && deployed_count < total_auto) {
        progress = 0;

        for (int i = 0; i < rt->manifest_count; i++) {
            if (!rt->manifests[i].auto_start)
                continue;

            /* Skip already deployed */
            int already = 0;
            for (int d = 0; d < deployed_count; d++) {
                if (deployed[d] == i) { already = 1; break; }
            }
            if (already)
                continue;

            /* Check if all dependencies are deployed */
            const struct openclaw_manifest *m = &rt->manifests[i];
            int deps_met = 1;

            for (int d = 0; d < m->depends_count; d++) {
                if (!is_deployed(deployed, deployed_count,
                                 rt, m->depends_on[d])) {
                    if (find_manifest(rt, m->depends_on[d]) < 0) {
                        rt_log("warning: %s depends on '%s' which has "
                               "no manifest - deploying anyway",
                               m->name, m->depends_on[d]);
                        continue;
                    }
                    deps_met = 0;
                    break;
                }
            }

            if (deps_met) {
                if (m->depends_count > 0)
                    rt_log("deploying %s (dependencies satisfied)", m->name);
                openclaw_deploy_manifest(rt, m);
                deployed[deployed_count++] = i;
                progress = 1;
            }
        }
    }

    /* Report any agents that couldn't be deployed */
    for (int i = 0; i < rt->manifest_count; i++) {
        if (!rt->manifests[i].auto_start)
            continue;
        int found = 0;
        for (int d = 0; d < deployed_count; d++) {
            if (deployed[d] == i) { found = 1; break; }
        }
        if (!found) {
            rt_log("ERROR: cannot deploy %s - unresolved dependencies:",
                   rt->manifests[i].name);
            for (int d = 0; d < rt->manifests[i].depends_count; d++) {
                if (!is_deployed(deployed, deployed_count,
                                 rt, rt->manifests[i].depends_on[d])) {
                    rt_log("  missing: %s", rt->manifests[i].depends_on[d]);
                }
            }
        }
    }
}

int openclaw_run(struct openclaw_runtime *rt)
{
    struct epoll_event events[32];

    rt->running = 1;

    /* Load manifests from default directory */
    openclaw_load_manifests_dir(rt, OPENCLAW_MANIFESTS_DIR);

    /* Deploy auto_start agents respecting dependency ordering */
    deploy_in_order(rt);

    rt_log("runtime ready (%d manifests loaded)", rt->manifest_count);

    while (rt->running && !got_signal) {
        int nfds = epoll_wait(rt->epoll_fd, events, 32, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == rt->sock_fd) {
                int client = accept4(rt->sock_fd, NULL, NULL, SOCK_CLOEXEC);
                if (client >= 0)
                    handle_api_request(rt, client);
            }
        }
    }

    return 0;
}

void openclaw_shutdown(struct openclaw_runtime *rt)
{
    rt_log("shutting down");

    if (rt->epoll_fd >= 0) close(rt->epoll_fd);
    if (rt->sock_fd >= 0) close(rt->sock_fd);
    unlink(OPENCLAW_SOCKET);
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    struct openclaw_runtime rt;
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

    if (openclaw_init(&rt, NULL) < 0) {
        fprintf(stderr, "openclaw: init failed\n");
        return 1;
    }

    openclaw_run(&rt);
    openclaw_shutdown(&rt);

    return 0;
}

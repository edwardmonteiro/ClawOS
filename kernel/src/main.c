/*
 * clawd - ClawOS Kernel Daemon
 *
 * The core daemon that manages agent lifecycles, IPC routing,
 * resource sandboxing, and extension loading. Sits on top of
 * the Linux kernel as a lightweight orchestration layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "claw/kernel.h"

static struct claw_kernel kernel;
static volatile int got_signal = 0;

static void signal_handler(int sig)
{
    (void)sig;
    got_signal = 1;
    kernel.running = 0;
}

static int create_pid_file(void)
{
    FILE *f = fopen(CLAWD_PID_FILE, "w");
    if (!f)
        return CLAW_ERR_IO;
    fprintf(f, "%d\n", getpid());
    fclose(f);
    return CLAW_OK;
}

static int setup_socket(struct claw_kernel *k)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return CLAW_ERR_IO;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLAWD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(CLAWD_SOCKET_PATH);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return CLAW_ERR_IO;
    }

    if (listen(fd, 32) < 0) {
        close(fd);
        return CLAW_ERR_IO;
    }

    chmod(CLAWD_SOCKET_PATH, 0660);
    k->sock_fd = fd;
    return CLAW_OK;
}

static int setup_epoll(struct claw_kernel *k)
{
    struct epoll_event ev;

    k->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (k->epoll_fd < 0)
        return CLAW_ERR_IO;

    ev.events = EPOLLIN;
    ev.data.fd = k->sock_fd;
    if (epoll_ctl(k->epoll_fd, EPOLL_CTL_ADD, k->sock_fd, &ev) < 0)
        return CLAW_ERR_IO;

    return CLAW_OK;
}

static void ensure_dirs(void)
{
    mkdir("/run/claw", 0755);
    mkdir("/run/claw/ipc", 0755);
    mkdir("/var/log/claw", 0755);
    mkdir(CLAWD_CGROUP_ROOT, 0755);
}

static void handle_client(struct claw_kernel *k, int client_fd)
{
    char buf[CLAW_MAX_MSG_SIZE];
    ssize_t n;

    n = read(client_fd, buf, sizeof(buf));
    if (n <= 0) {
        close(client_fd);
        return;
    }

    /* Parse incoming message as claw_msg */
    if ((size_t)n < sizeof(struct claw_msg)) {
        close(client_fd);
        return;
    }

    struct claw_msg *msg = (struct claw_msg *)buf;

    switch (msg->type) {
    case CLAW_MSG_REQUEST:
        claw_log(CLAW_LOG_DEBUG, "request from agent %lu: %s",
                 msg->src, msg->topic);
        /* Route to destination agent or handle internally */
        if (msg->dst != 0) {
            struct claw_agent *dst = claw_agent_find(k, msg->dst);
            if (dst) {
                /* Forward message to destination */
                claw_log(CLAW_LOG_DEBUG, "routing to agent %s", dst->name);
            }
        }
        break;

    case CLAW_MSG_EVENT:
        claw_log(CLAW_LOG_DEBUG, "event from agent %lu: %s",
                 msg->src, msg->topic);
        break;

    case CLAW_MSG_SIGNAL:
        claw_log(CLAW_LOG_DEBUG, "signal from agent %lu", msg->src);
        break;

    default:
        claw_log(CLAW_LOG_WARN, "unknown message type %d", msg->type);
        break;
    }

    close(client_fd);
}

/* ---- Kernel lifecycle ---- */

int claw_kernel_init(struct claw_kernel *k, const char *config)
{
    int rc;

    memset(k, 0, sizeof(*k));
    k->running = 0;
    k->log_level = CLAW_LOG_INFO;

    if (config)
        strncpy(k->config_path, config, sizeof(k->config_path) - 1);
    else
        strncpy(k->config_path, CLAWD_CONFIG_PATH,
                sizeof(k->config_path) - 1);

    ensure_dirs();

    rc = setup_socket(k);
    if (rc != CLAW_OK) {
        claw_log(CLAW_LOG_ERROR, "failed to create socket: %s",
                 strerror(errno));
        return rc;
    }

    rc = setup_epoll(k);
    if (rc != CLAW_OK) {
        claw_log(CLAW_LOG_ERROR, "failed to create epoll: %s",
                 strerror(errno));
        return rc;
    }

    claw_log(CLAW_LOG_INFO, "clawd v%s initialized", CLAWD_VERSION_STRING);
    return CLAW_OK;
}

int claw_kernel_run(struct claw_kernel *k)
{
    struct epoll_event events[64];
    int nfds, i;

    k->running = 1;
    create_pid_file();

    claw_log(CLAW_LOG_INFO, "clawd running (pid %d)", getpid());

    /* Load extensions from default directory */
    claw_ext_load_dir(k, CLAWD_EXT_DIR);

    while (k->running) {
        nfds = epoll_wait(k->epoll_fd, events, 64, 1000);
        if (nfds < 0) {
            if (errno == EINTR)
                continue;
            claw_log(CLAW_LOG_ERROR, "epoll_wait: %s", strerror(errno));
            break;
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == k->sock_fd) {
                /* New connection */
                int client = accept4(k->sock_fd, NULL, NULL,
                                     SOCK_CLOEXEC);
                if (client >= 0)
                    handle_client(k, client);
            }
        }

        /* Reap dead child processes */
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int j = 0; j < k->agent_count; j++) {
                if (k->agents[j].pid == pid) {
                    claw_log(CLAW_LOG_INFO, "agent %s (pid %d) exited",
                             k->agents[j].name, pid);
                    k->agents[j].state = CLAW_AGENT_DEAD;
                    k->agents[j].pid = 0;
                    break;
                }
            }
        }
    }

    return CLAW_OK;
}

void claw_kernel_shutdown(struct claw_kernel *k)
{
    claw_log(CLAW_LOG_INFO, "shutting down...");

    /* Stop all agents */
    for (int i = 0; i < k->agent_count; i++) {
        if (k->agents[i].state == CLAW_AGENT_RUNNING)
            claw_agent_stop(k, k->agents[i].id);
    }

    /* Unload all extensions */
    for (int i = 0; i < k->ext_count; i++) {
        if (k->extensions[i].loaded)
            claw_ext_unload(k, k->extensions[i].id);
    }

    if (k->epoll_fd >= 0)
        close(k->epoll_fd);
    if (k->sock_fd >= 0)
        close(k->sock_fd);

    unlink(CLAWD_SOCKET_PATH);
    unlink(CLAWD_PID_FILE);

    claw_log(CLAW_LOG_INFO, "clawd stopped");
}

/* ---- Main ---- */

static void usage(void)
{
    fprintf(stderr,
        "Usage: clawd [options]\n"
        "\n"
        "ClawOS Kernel Daemon v%s\n"
        "\n"
        "Options:\n"
        "  -c <path>    Configuration file (default: %s)\n"
        "  -d           Debug mode (verbose logging)\n"
        "  -f           Run in foreground\n"
        "  -v           Show version\n"
        "  -h           Show this help\n",
        CLAWD_VERSION_STRING, CLAWD_CONFIG_PATH);
}

int main(int argc, char **argv)
{
    int opt;
    int foreground = 0;
    const char *config = NULL;

    while ((opt = getopt(argc, argv, "c:dfvh")) != -1) {
        switch (opt) {
        case 'c':
            config = optarg;
            break;
        case 'd':
            kernel.log_level = CLAW_LOG_DEBUG;
            break;
        case 'f':
            foreground = 1;
            break;
        case 'v':
            printf("clawd v%s\n", CLAWD_VERSION_STRING);
            return 0;
        case 'h':
        default:
            usage();
            return opt == 'h' ? 0 : 1;
        }
    }

    /* Daemonize unless foreground mode */
    if (!foreground) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid > 0)
            return 0;  /* parent exits */
        setsid();
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGCHLD, SIG_DFL);

    if (claw_kernel_init(&kernel, config) != CLAW_OK) {
        fprintf(stderr, "clawd: initialization failed\n");
        return 1;
    }

    claw_kernel_run(&kernel);
    claw_kernel_shutdown(&kernel);

    return 0;
}

/*
 * claw - ClawOS Command Line Interface
 *
 * Main entry point for the `claw` CLI tool.
 * Provides commands for agent management, extension control,
 * system status, and OpenClaw operations.
 *
 * Usage:
 *   claw status                 - Show system status
 *   claw agent list             - List running agents
 *   claw agent create <name>    - Create a new agent
 *   claw agent start <name>     - Start an agent
 *   claw agent stop <name>      - Stop an agent
 *   claw agent destroy <name>   - Destroy an agent
 *   claw deploy <manifest>      - Deploy from manifest file
 *   claw ext list               - List loaded extensions
 *   claw ext load <path>        - Load an extension
 *   claw bus pub <topic> <msg>  - Publish message to bus
 *   claw version                - Show version info
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define CLAWD_SOCKET    "/run/claw/clawd.sock"
#define OPENCLAW_SOCKET "/run/claw/openclaw.sock"
#define CLAW_BUS_SOCKET "/run/claw/bus.sock"

#define VERSION "0.1.0"

static int connect_socket(const char *path)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fprintf(stderr, "claw: cannot create socket\n");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int send_command(const char *socket_path, const char *cmd,
                       char *response, size_t resp_size)
{
    int fd = connect_socket(socket_path);
    if (fd < 0) {
        fprintf(stderr, "claw: cannot connect to %s\n", socket_path);
        return -1;
    }

    write(fd, cmd, strlen(cmd));

    ssize_t n = read(fd, response, resp_size - 1);
    close(fd);

    if (n > 0) {
        response[n] = '\0';
        return 0;
    }

    return -1;
}

/* ---- Commands ---- */

static int cmd_status(void)
{
    char resp[4096];

    if (send_command(OPENCLAW_SOCKET, "STATUS", resp, sizeof(resp)) == 0) {
        printf("ClawOS Status:\n");
        printf("  %s\n", resp);
        return 0;
    }

    /* Try clawd directly */
    int fd = connect_socket(CLAWD_SOCKET);
    if (fd >= 0) {
        printf("ClawOS Status:\n");
        printf("  clawd: running\n");
        close(fd);
    } else {
        printf("ClawOS Status:\n");
        printf("  clawd: not running\n");
    }

    fd = connect_socket(CLAW_BUS_SOCKET);
    if (fd >= 0) {
        printf("  claw-bus: running\n");
        close(fd);
    } else {
        printf("  claw-bus: not running\n");
    }

    fd = connect_socket(OPENCLAW_SOCKET);
    if (fd >= 0) {
        printf("  openclaw: running\n");
        close(fd);
    } else {
        printf("  openclaw: not running\n");
    }

    return 0;
}

static int cmd_agent_list(void)
{
    char resp[8192];

    if (send_command(OPENCLAW_SOCKET, "LIST", resp, sizeof(resp)) == 0) {
        printf("%s\n", resp);
        return 0;
    }

    fprintf(stderr, "claw: cannot connect to openclaw runtime\n");
    return 1;
}

static int cmd_deploy(const char *manifest_path)
{
    char cmd[512];
    char resp[4096];

    snprintf(cmd, sizeof(cmd), "DEPLOY %s", manifest_path);

    if (send_command(OPENCLAW_SOCKET, cmd, resp, sizeof(resp)) == 0) {
        printf("%s\n", resp);
        return 0;
    }

    fprintf(stderr, "claw: cannot connect to openclaw runtime\n");
    return 1;
}

static int cmd_version(void)
{
    printf("ClawOS v%s\n", VERSION);
    printf("  clawd:    kernel daemon\n");
    printf("  claw-bus: message bus\n");
    printf("  openclaw: integration engine\n");
    return 0;
}

/* ---- Usage ---- */

static void usage(void)
{
    printf(
        "Usage: claw <command> [options]\n"
        "\n"
        "ClawOS CLI v%s - The OS for Modern Agents\n"
        "\n"
        "Commands:\n"
        "  status              Show system status\n"
        "  agent list          List agents\n"
        "  agent create <n>    Create agent\n"
        "  agent start <n>     Start agent\n"
        "  agent stop <n>      Stop agent\n"
        "  agent destroy <n>   Destroy agent\n"
        "  deploy <manifest>   Deploy from manifest\n"
        "  ext list            List extensions\n"
        "  ext load <path>     Load extension\n"
        "  bus pub <t> <msg>   Publish to bus topic\n"
        "  version             Show version\n"
        "  help                Show this help\n",
        VERSION);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    }
    else if (strcmp(cmd, "agent") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: claw agent <list|create|start|stop|destroy>\n");
            return 1;
        }
        if (strcmp(argv[2], "list") == 0)
            return cmd_agent_list();
        fprintf(stderr, "claw: agent %s - not yet implemented\n", argv[2]);
        return 1;
    }
    else if (strcmp(cmd, "deploy") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: claw deploy <manifest.toml>\n");
            return 1;
        }
        return cmd_deploy(argv[2]);
    }
    else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "-v") == 0) {
        return cmd_version();
    }
    else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
             strcmp(cmd, "--help") == 0) {
        usage();
        return 0;
    }
    else {
        fprintf(stderr, "claw: unknown command '%s'\n", cmd);
        usage();
        return 1;
    }
}

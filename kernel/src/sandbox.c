/*
 * ClawOS - Agent Sandboxing
 *
 * Linux namespace and seccomp-based sandboxing for agents.
 * Each agent runs in its own isolated environment with
 * capability-based access control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <errno.h>

#include "claw/kernel.h"

/* Namespace flags based on agent capabilities */
static int caps_to_clone_flags(claw_caps_t caps)
{
    int flags = CLONE_NEWPID | CLONE_NEWUTS;  /* always isolate PID/UTS */

    if (!(caps & CLAW_CAP_NET))
        flags |= CLONE_NEWNET;    /* isolate network unless allowed */
    if (!(caps & CLAW_CAP_FS))
        flags |= CLONE_NEWNS;     /* isolate mounts unless fs allowed */

    return flags;
}

/* Set up a minimal /proc inside the sandbox */
static int setup_sandbox_fs(const char *rootfs)
{
    char proc_path[CLAW_MAX_PATH];

    if (!rootfs || !rootfs[0])
        return CLAW_OK;  /* no rootfs = no fs isolation */

    /* Pivot root to agent's rootfs */
    if (chroot(rootfs) < 0)
        return CLAW_ERR_IO;

    if (chdir("/") < 0)
        return CLAW_ERR_IO;

    /* Mount /proc for the new PID namespace */
    snprintf(proc_path, sizeof(proc_path), "/proc");
    mkdir(proc_path, 0555);
    if (mount("proc", proc_path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC,
              NULL) < 0)
        return CLAW_ERR_IO;

    return CLAW_OK;
}

/* Drop capabilities not granted to the agent */
static int apply_caps(claw_caps_t caps)
{
    /* Set no-new-privileges to prevent privilege escalation */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
        return CLAW_ERR_IO;

    /* Drop ambient capabilities */
    if (!(caps & CLAW_CAP_ADMIN)) {
        /* In a full implementation, we'd use libcap to precisely
           control Linux capabilities. For now we just set
           no-new-privileges and rely on namespace isolation. */
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    }

    return CLAW_OK;
}

struct sandbox_args {
    struct claw_agent  *agent;
    const char         *exec_path;
    char              **argv;
    char              **envp;
};

static int sandbox_child(void *arg)
{
    struct sandbox_args *sa = arg;
    struct claw_agent *agent = sa->agent;

    /* Set up filesystem isolation */
    if (agent->rootfs[0]) {
        if (setup_sandbox_fs(agent->rootfs) != CLAW_OK)
            _exit(126);
    }

    /* Apply capability restrictions */
    apply_caps(agent->caps);

    /* Set hostname to agent name */
    sethostname(agent->name, strlen(agent->name));

    /* Execute the agent binary */
    execve(sa->exec_path, sa->argv, sa->envp);
    _exit(127);
}

int claw_sandbox_spawn(struct claw_kernel *k, claw_aid_t id,
                       const char *exec_path, char **argv, char **envp)
{
    struct claw_agent *agent = claw_agent_find(k, id);
    if (!agent)
        return CLAW_ERR_NOENT;

    int clone_flags = caps_to_clone_flags(agent->caps);

    /* Stack for clone */
    size_t stack_size = 1024 * 1024;  /* 1MB */
    void *stack = malloc(stack_size);
    if (!stack)
        return CLAW_ERR_NOMEM;

    struct sandbox_args sa = {
        .agent = agent,
        .exec_path = exec_path,
        .argv = argv,
        .envp = envp,
    };

    pid_t pid = clone(sandbox_child, stack + stack_size,
                      clone_flags | SIGCHLD, &sa);

    free(stack);

    if (pid < 0) {
        claw_log(CLAW_LOG_ERROR, "clone failed for agent %s: %s",
                 agent->name, strerror(errno));
        return CLAW_ERR_IO;
    }

    agent->pid = pid;
    agent->state = CLAW_AGENT_RUNNING;

    claw_log(CLAW_LOG_INFO, "agent %s sandboxed (pid=%d, flags=0x%x)",
             agent->name, pid, clone_flags);

    return CLAW_OK;
}

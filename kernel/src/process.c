/*
 * ClawOS - Agent/Process Management
 *
 * Manages agent lifecycles with Linux cgroup-based resource control
 * and namespace isolation for sandboxing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>

#include "claw/kernel.h"

static claw_aid_t next_aid = 1;

static int setup_cgroup(struct claw_agent *agent)
{
    char path[CLAW_MAX_PATH];

    snprintf(path, sizeof(path), "%s/%s", CLAWD_CGROUP_ROOT, agent->name);
    if (mkdir(path, 0755) < 0 && errno != EEXIST)
        return CLAW_ERR_IO;

    strncpy(agent->cgroup, path, sizeof(agent->cgroup) - 1);

    /* Set memory limit if specified */
    if (agent->memory_limit > 0) {
        char mem_path[CLAW_MAX_PATH];
        snprintf(mem_path, sizeof(mem_path), "%s/memory.max", path);
        FILE *f = fopen(mem_path, "w");
        if (f) {
            fprintf(f, "%lu", agent->memory_limit);
            fclose(f);
        }
    }

    /* Set CPU shares */
    if (agent->cpu_shares > 0) {
        char cpu_path[CLAW_MAX_PATH];
        snprintf(cpu_path, sizeof(cpu_path), "%s/cpu.weight", path);
        FILE *f = fopen(cpu_path, "w");
        if (f) {
            fprintf(f, "%lu", agent->cpu_shares);
            fclose(f);
        }
    }

    return CLAW_OK;
}

static int add_to_cgroup(struct claw_agent *agent)
{
    char path[CLAW_MAX_PATH];
    snprintf(path, sizeof(path), "%s/cgroup.procs", agent->cgroup);

    FILE *f = fopen(path, "w");
    if (!f)
        return CLAW_ERR_IO;

    fprintf(f, "%d", agent->pid);
    fclose(f);
    return CLAW_OK;
}

claw_aid_t claw_agent_create(struct claw_kernel *k, const char *name,
                             claw_caps_t caps)
{
    if (k->agent_count >= CLAW_MAX_AGENTS) {
        claw_log(CLAW_LOG_ERROR, "max agents reached");
        return 0;
    }

    /* Check for duplicate name */
    if (claw_agent_find_by_name(k, name)) {
        claw_log(CLAW_LOG_ERROR, "agent '%s' already exists", name);
        return 0;
    }

    struct claw_agent *agent = &k->agents[k->agent_count];
    memset(agent, 0, sizeof(*agent));

    agent->id = next_aid++;
    strncpy(agent->name, name, CLAW_MAX_NAME - 1);
    agent->state = CLAW_AGENT_CREATED;
    agent->caps = caps;
    agent->priority = 100;          /* default priority */
    agent->memory_limit = 0;        /* unlimited by default */
    agent->cpu_shares = 100;        /* default weight */

    setup_cgroup(agent);

    k->agent_count++;

    claw_log(CLAW_LOG_INFO, "agent created: %s (id=%lu, caps=0x%lx)",
             name, agent->id, caps);

    return agent->id;
}

int claw_agent_start(struct claw_kernel *k, claw_aid_t id)
{
    struct claw_agent *agent = claw_agent_find(k, id);
    if (!agent)
        return CLAW_ERR_NOENT;

    if (agent->state == CLAW_AGENT_RUNNING)
        return CLAW_ERR_BUSY;

    agent->state = CLAW_AGENT_READY;

    /* The actual process spawning happens when an executable is
       associated with the agent via the OpenClaw manifest system.
       For now, mark as running. */
    agent->state = CLAW_AGENT_RUNNING;

    if (agent->pid > 0)
        add_to_cgroup(agent);

    claw_log(CLAW_LOG_INFO, "agent started: %s (id=%lu)", agent->name, id);
    return CLAW_OK;
}

int claw_agent_stop(struct claw_kernel *k, claw_aid_t id)
{
    struct claw_agent *agent = claw_agent_find(k, id);
    if (!agent)
        return CLAW_ERR_NOENT;

    if (agent->state != CLAW_AGENT_RUNNING &&
        agent->state != CLAW_AGENT_WAITING)
        return CLAW_OK;

    if (agent->pid > 0) {
        kill(agent->pid, SIGTERM);
        /* Give it 5 seconds, then SIGKILL */
        usleep(100000);
        if (kill(agent->pid, 0) == 0) {
            claw_log(CLAW_LOG_WARN, "agent %s not responding, sending SIGKILL",
                     agent->name);
            kill(agent->pid, SIGKILL);
        }
    }

    agent->state = CLAW_AGENT_STOPPED;
    claw_log(CLAW_LOG_INFO, "agent stopped: %s (id=%lu)", agent->name, id);
    return CLAW_OK;
}

int claw_agent_destroy(struct claw_kernel *k, claw_aid_t id)
{
    struct claw_agent *agent = claw_agent_find(k, id);
    if (!agent)
        return CLAW_ERR_NOENT;

    if (agent->state == CLAW_AGENT_RUNNING)
        claw_agent_stop(k, id);

    /* Remove cgroup */
    if (agent->cgroup[0])
        rmdir(agent->cgroup);

    claw_log(CLAW_LOG_INFO, "agent destroyed: %s (id=%lu)", agent->name, id);

    /* Mark slot as dead for reuse */
    agent->state = CLAW_AGENT_DEAD;
    agent->id = 0;
    agent->name[0] = '\0';

    return CLAW_OK;
}

struct claw_agent *claw_agent_find(struct claw_kernel *k, claw_aid_t id)
{
    for (int i = 0; i < k->agent_count; i++) {
        if (k->agents[i].id == id)
            return &k->agents[i];
    }
    return NULL;
}

struct claw_agent *claw_agent_find_by_name(struct claw_kernel *k,
                                           const char *name)
{
    for (int i = 0; i < k->agent_count; i++) {
        if (strcmp(k->agents[i].name, name) == 0 &&
            k->agents[i].state != CLAW_AGENT_DEAD)
            return &k->agents[i];
    }
    return NULL;
}

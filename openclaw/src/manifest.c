/*
 * OpenClaw Manifest Parser
 *
 * Parses simple TOML-like manifest files for agent declarations.
 * Intentionally lightweight - no external dependencies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

#include "openclaw/manifest.h"

static void trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s)) s++;

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

static void strip_quotes(char *s)
{
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static uint64_t parse_size(const char *s)
{
    char *end;
    uint64_t val = strtoull(s, &end, 10);

    switch (toupper((unsigned char)*end)) {
    case 'K': val *= 1024; break;
    case 'M': val *= 1024 * 1024; break;
    case 'G': val *= 1024ULL * 1024 * 1024; break;
    }

    return val;
}

static claw_caps_t parse_capability(const char *name, int value)
{
    if (!value) return 0;

    if (strcmp(name, "net") == 0)      return CLAW_CAP_NET;
    if (strcmp(name, "fs") == 0)       return CLAW_CAP_FS;
    if (strcmp(name, "proc") == 0)     return CLAW_CAP_PROC;
    if (strcmp(name, "ipc") == 0)      return CLAW_CAP_IPC;
    if (strcmp(name, "hw") == 0)       return CLAW_CAP_HW;
    if (strcmp(name, "ext") == 0)      return CLAW_CAP_EXT;
    if (strcmp(name, "openclaw") == 0) return CLAW_CAP_OPENCLAW;
    if (strcmp(name, "admin") == 0)    return CLAW_CAP_ADMIN;
    if (strcmp(name, "bus") == 0)      return CLAW_CAP_BUS;
    if (strcmp(name, "sandbox") == 0)  return CLAW_CAP_SANDBOX;

    return 0;
}

int openclaw_manifest_parse(const char *path, struct openclaw_manifest *m)
{
    FILE *f;
    char line[512];
    char section[64] = "";

    memset(m, 0, sizeof(*m));

    f = fopen(path, "r");
    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        trim(p);

        /* Skip empty lines and comments */
        if (p[0] == '\0' || p[0] == '#')
            continue;

        /* Section header */
        if (p[0] == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                strncpy(section, p + 1, sizeof(section) - 1);
            }
            continue;
        }

        /* Key = value */
        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        trim(key);
        trim(val);
        strip_quotes(val);

        if (strcmp(section, "agent") == 0) {
            if (strcmp(key, "name") == 0)
                strncpy(m->name, val, CLAW_MAX_NAME - 1);
            else if (strcmp(key, "version") == 0)
                strncpy(m->version, val, sizeof(m->version) - 1);
            else if (strcmp(key, "description") == 0)
                strncpy(m->description, val, sizeof(m->description) - 1);
            else if (strcmp(key, "exec") == 0)
                strncpy(m->exec_path, val, CLAW_MAX_PATH - 1);
        }
        else if (strcmp(section, "resources") == 0) {
            if (strcmp(key, "memory") == 0)
                m->memory_limit = parse_size(val);
            else if (strcmp(key, "cpu_shares") == 0)
                m->cpu_shares = strtoull(val, NULL, 10);
        }
        else if (strcmp(section, "capabilities") == 0) {
            int enabled = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
            m->caps |= parse_capability(key, enabled);
        }
        else if (strcmp(section, "lifecycle") == 0) {
            int bval = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
            if (strcmp(key, "auto_start") == 0)
                m->auto_start = bval;
            else if (strcmp(key, "restart_on_failure") == 0)
                m->restart_on_failure = bval;
            else if (strcmp(key, "max_restarts") == 0)
                m->max_restarts = atoi(val);
        }
        else if (strcmp(section, "dependencies") == 0) {
            if (strcmp(key, "requires") == 0) {
                /* Parse comma-separated list */
                char *tok = strtok(val, ",");
                while (tok && m->depends_count < 8) {
                    trim(tok);
                    strip_quotes(tok);
                    strncpy(m->depends_on[m->depends_count], tok,
                            CLAW_MAX_NAME - 1);
                    m->depends_count++;
                    tok = strtok(NULL, ",");
                }
            }
        }
        else if (strcmp(section, "bus") == 0) {
            if (strcmp(key, "subscribe") == 0) {
                char *tok = strtok(val, ",");
                while (tok && m->topic_count < 16) {
                    trim(tok);
                    strip_quotes(tok);
                    strncpy(m->topics[m->topic_count], tok,
                            CLAW_MAX_NAME - 1);
                    m->topic_count++;
                    tok = strtok(NULL, ",");
                }
            }
        }
        else if (strcmp(section, "environment") == 0) {
            if (m->env_count < 32) {
                snprintf(m->env[m->env_count], CLAW_MAX_PATH,
                         "%s=%s", key, val);
                m->env_count++;
            }
        }
    }

    fclose(f);
    return openclaw_manifest_validate(m);
}

int openclaw_manifest_validate(const struct openclaw_manifest *m)
{
    if (m->name[0] == '\0')
        return -1;
    if (m->exec_path[0] == '\0')
        return -1;
    return 0;
}

int openclaw_manifest_to_string(const struct openclaw_manifest *m,
                                char *buf, size_t buflen)
{
    return snprintf(buf, buflen,
        "{\"name\":\"%s\",\"version\":\"%s\",\"description\":\"%s\","
        "\"exec\":\"%s\",\"caps\":%lu,\"memory_limit\":%lu,"
        "\"cpu_shares\":%lu,\"auto_start\":%s,"
        "\"restart_on_failure\":%s,\"max_restarts\":%d}",
        m->name, m->version, m->description,
        m->exec_path, m->caps, m->memory_limit,
        m->cpu_shares,
        m->auto_start ? "true" : "false",
        m->restart_on_failure ? "true" : "false",
        m->max_restarts);
}

int openclaw_load_manifest(struct openclaw_runtime *rt, const char *path)
{
    if (rt->manifest_count >= CLAW_MAX_AGENTS)
        return -1;

    struct openclaw_manifest *m = &rt->manifests[rt->manifest_count];
    if (openclaw_manifest_parse(path, m) != 0) {
        fprintf(stderr, "[openclaw] invalid manifest: %s\n", path);
        return -1;
    }

    rt->manifest_count++;
    fprintf(stderr, "[openclaw] loaded manifest: %s v%s\n",
            m->name, m->version);
    return 0;
}

int openclaw_load_manifests_dir(struct openclaw_runtime *rt, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;

    struct dirent *ent;
    int count = 0;

    while ((ent = readdir(d)) != NULL) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".toml") != 0)
            continue;

        char path[CLAW_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if (openclaw_load_manifest(rt, path) == 0)
            count++;
    }

    closedir(d);
    return count;
}

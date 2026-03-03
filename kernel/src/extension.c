/*
 * ClawOS - Extension Loading
 *
 * Dynamic shared library based extension system.
 * Extensions are .so files loaded at runtime that can
 * register new capabilities, message handlers, and agents.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>

#include "claw/kernel.h"

static claw_eid_t next_eid = 1;

int claw_ext_load(struct claw_kernel *k, const char *path)
{
    if (k->ext_count >= CLAW_MAX_EXTENSIONS) {
        claw_log(CLAW_LOG_ERROR, "max extensions reached");
        return CLAW_ERR_FULL;
    }

    struct claw_extension *ext = &k->extensions[k->ext_count];
    memset(ext, 0, sizeof(*ext));

    ext->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!ext->handle) {
        claw_log(CLAW_LOG_ERROR, "failed to load extension %s: %s",
                 path, dlerror());
        return CLAW_ERR_IO;
    }

    /* Look up required symbols */
    ext->init = dlsym(ext->handle, "claw_ext_init");
    ext->cleanup = dlsym(ext->handle, "claw_ext_cleanup");

    /* Extension name from symbol or filename */
    const char *(*get_name)(void) = dlsym(ext->handle, "claw_ext_name");
    if (get_name) {
        strncpy(ext->name, get_name(), CLAW_MAX_NAME - 1);
    } else {
        /* Extract name from filename */
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        strncpy(ext->name, base, CLAW_MAX_NAME - 1);
    }

    strncpy(ext->path, path, CLAW_MAX_PATH - 1);
    ext->id = next_eid++;

    /* Initialize the extension */
    if (ext->init) {
        int rc = ext->init();
        if (rc != 0) {
            claw_log(CLAW_LOG_ERROR, "extension %s init failed (rc=%d)",
                     ext->name, rc);
            dlclose(ext->handle);
            memset(ext, 0, sizeof(*ext));
            return CLAW_ERR;
        }
    }

    ext->loaded = 1;
    k->ext_count++;

    claw_log(CLAW_LOG_INFO, "extension loaded: %s (id=%u)", ext->name, ext->id);
    return CLAW_OK;
}

int claw_ext_unload(struct claw_kernel *k, claw_eid_t id)
{
    for (int i = 0; i < k->ext_count; i++) {
        if (k->extensions[i].id == id && k->extensions[i].loaded) {
            struct claw_extension *ext = &k->extensions[i];

            if (ext->cleanup)
                ext->cleanup();

            if (ext->handle)
                dlclose(ext->handle);

            claw_log(CLAW_LOG_INFO, "extension unloaded: %s", ext->name);
            ext->loaded = 0;
            return CLAW_OK;
        }
    }
    return CLAW_ERR_NOENT;
}

int claw_ext_load_dir(struct claw_kernel *k, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        if (errno == ENOENT)
            return CLAW_OK;  /* no extensions directory is fine */
        return CLAW_ERR_IO;
    }

    struct dirent *ent;
    int count = 0;

    while ((ent = readdir(d)) != NULL) {
        /* Only load .so files */
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".so") != 0)
            continue;

        char path[CLAW_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if (claw_ext_load(k, path) == CLAW_OK)
            count++;
    }

    closedir(d);
    claw_log(CLAW_LOG_INFO, "loaded %d extensions from %s", count, dir);
    return CLAW_OK;
}

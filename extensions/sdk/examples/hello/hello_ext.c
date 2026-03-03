/*
 * Hello Extension - Example ClawOS Extension
 *
 * A minimal example showing how to build a ClawOS extension.
 * This extension registers itself and responds to "hello" messages.
 *
 * Build:
 *   gcc -shared -fPIC -o hello.so hello_ext.c
 *
 * Install:
 *   cp hello.so /usr/lib/claw/extensions/
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "claw_ext.h"

const char *claw_ext_name(void)
{
    return "hello";
}

const char *claw_ext_version(void)
{
    return "0.1.0";
}

int claw_ext_init(void)
{
    fprintf(stderr, "[ext:hello] initialized\n");
    return 0;
}

void claw_ext_cleanup(void)
{
    fprintf(stderr, "[ext:hello] cleaned up\n");
}

int claw_ext_handle_msg(const struct claw_msg *msg,
                        void *reply, uint32_t *reply_len)
{
    if (strcmp(msg->topic, "hello") != 0)
        return CLAW_ERR_NOENT;

    const char *response = "Hello from ClawOS!";
    size_t len = strlen(response) + 1;

    if (reply && reply_len && *reply_len >= len) {
        memcpy(reply, response, len);
        *reply_len = len;
    }

    fprintf(stderr, "[ext:hello] handled hello message from agent %lu\n",
            msg->src);
    return CLAW_OK;
}

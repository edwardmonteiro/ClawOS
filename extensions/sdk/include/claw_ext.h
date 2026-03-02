/*
 * ClawOS Extension SDK
 *
 * Header for building ClawOS extensions (plugins).
 * Extensions are shared libraries (.so) loaded by clawd at runtime.
 *
 * To create an extension:
 *   1. Include this header
 *   2. Implement claw_ext_init() and claw_ext_cleanup()
 *   3. Optionally implement claw_ext_name() and claw_ext_version()
 *   4. Compile as shared library: gcc -shared -fPIC -o myext.so myext.c
 *   5. Place in /usr/lib/claw/extensions/
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLAW_EXT_H
#define CLAW_EXT_H

#include "../../kernel/include/claw/types.h"

/*
 * Required: Initialize the extension.
 * Return 0 on success, non-zero on failure.
 * Called when clawd loads the extension.
 */
int claw_ext_init(void);

/*
 * Required: Clean up the extension.
 * Called when clawd unloads the extension or shuts down.
 */
void claw_ext_cleanup(void);

/*
 * Optional: Return the extension name.
 * If not provided, the filename is used.
 */
const char *claw_ext_name(void);

/*
 * Optional: Return the extension version string.
 */
const char *claw_ext_version(void);

/*
 * Optional: Handle a message directed at this extension.
 * Return 0 if handled, CLAW_ERR_NOENT to pass through.
 */
int claw_ext_handle_msg(const struct claw_msg *msg,
                        void *reply, uint32_t *reply_len);

#endif /* CLAW_EXT_H */

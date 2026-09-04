#ifndef NEO_COMMAND_TOOLS_H
#define NEO_COMMAND_TOOLS_H

#include "config.h"
#include <stddef.h>

/* Return index of command by name, or -1. */
int command_tool_find(const agent_config_t *conf, const char *name);

/*
 * Run a declared command tool under root_real.
 * On success returns 0 and sets *out_text (caller frees). Non-zero process
 * exit still returns 0 with EXIT:<code>\\n prefix. Fatal setup errors return -1.
 */
int command_tool_run(const agent_config_t *conf, const char *root_real, const tool_command_t *cmd,
                     const char *args_json, char **out_text, size_t *out_len);

#endif

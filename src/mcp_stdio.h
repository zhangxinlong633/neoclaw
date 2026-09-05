#ifndef NEO_MCP_STDIO_H
#define NEO_MCP_STDIO_H

#include "capability_matrix.h"
#include "config.h"

/* Spawn/list MCP stdio servers into matrix. Failures skip that server. Returns 0. */
int mcp_stdio_load_into_matrix(const agent_config_t *conf, capability_matrix_t *m);

/* Call an MCP tool row. Caller frees *out. Returns 0 on handled call. */
int mcp_stdio_call(const agent_config_t *conf, const cap_row_t *row, const char *args_json,
                   char **out, size_t *out_len);

void mcp_stdio_shutdown_all(void);

#endif

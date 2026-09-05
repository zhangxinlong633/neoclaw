#ifndef NEO_MCP_STDIO_H
#define NEO_MCP_STDIO_H

#include "capability_matrix.h"
#include "config.h"

/*
 * MCP stdio 传输：把远端 tools/list 灌进 Capability Matrix，不是第二套工具注册表。
 * 单 server 失败则跳过该 server，其它行继续可用。
 */

/* 启动各 mcp_servers、initialize、tools/list，写入矩阵行（名 mcp_<server>_<tool>）。 */
int mcp_stdio_load_into_matrix(const agent_config_t *conf, capability_matrix_t *m);

/* 对矩阵中的 MCP 行做 tools/call；规范化 content 为字符串结果。调用方 free *out。 */
int mcp_stdio_call(const agent_config_t *conf, const cap_row_t *row, const char *args_json,
                   char **out, size_t *out_len);

/* 关掉已拉起的 MCP 子进程（进程退出前调用）。 */
void mcp_stdio_shutdown_all(void);

#endif

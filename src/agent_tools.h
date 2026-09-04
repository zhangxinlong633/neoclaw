#ifndef NEO_AGENT_TOOLS_H
#define NEO_AGENT_TOOLS_H

#include "config.h"
#include "llm.h"

/*
 * Run chat/completions with OpenAI-style tools (read_file, write_file) under tools.root.
 * prefix_messages: prior user/assistant turns (daemon history); may be NULL if n_prefix==0.
 * On success, out_text holds final assistant text (caller llm_response_free).
 */
int agent_run_with_tools(
  const agent_config_t *conf,
  const char *system_prompt,
  const llm_message_t *prefix_messages,
  int n_prefix,
  const char *user_message,
  llm_response_t *out_text);

/*
 * Dispatch one tool by name (builtin or tools.commands).
 * Caller frees *out_text. Returns 0 on handled call (including tool ERROR strings).
 */
int neo_dispatch_tool(const agent_config_t *conf, const char *root_real,
                      const char *name, const char *args_json,
                      char **out_text, size_t *out_len);

#endif

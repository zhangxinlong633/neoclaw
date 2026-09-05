#ifndef NEO_CAPABILITY_MATRIX_H
#define NEO_CAPABILITY_MATRIX_H

#include "config.h"
#include <stddef.h>
#include <stdio.h>

typedef enum {
  CAP_SRC_BUILTIN = 0,
  CAP_SRC_COMMAND = 1,
  CAP_SRC_MCP = 2
} cap_source_t;

typedef enum {
  CAP_EFFECT_READ = 0,
  CAP_EFFECT_WRITE = 1,
  CAP_EFFECT_EXEC = 2,
  CAP_EFFECT_NETWORK = 3
} cap_effect_t;

/* builtin_id values for path tools (0 = none / not a builtin binding) */
enum {
  CAP_BUILTIN_NONE = 0,
  CAP_BUILTIN_READ_FILE = 1,
  CAP_BUILTIN_WRITE_FILE = 2,
  CAP_BUILTIN_LIST_DIR = 3,
  CAP_BUILTIN_HTTP_GET = 4,
  CAP_BUILTIN_GREP = 5,
  CAP_BUILTIN_RUN_COMMAND = 6
};

typedef struct {
  char *name;
  cap_source_t source;
  char *source_detail; /* e.g. mcp server name; may be NULL */
  char *description;
  char *parameters_json; /* JSON Schema object text */
  cap_effect_t effect;
  int timeout_sec;
  int max_output_bytes;
  int enabled;
  int builtin_id;
  const tool_command_t *command; /* non-owning; NULL if not command */
  char *mcp_server;
  char *mcp_tool;
} cap_row_t;

typedef struct {
  cap_row_t *rows;
  int count;
  int cap;
} capability_matrix_t;

void capability_matrix_init(capability_matrix_t *m);
void capability_matrix_free(capability_matrix_t *m);
const cap_row_t *capability_matrix_find(const capability_matrix_t *m, const char *name);

/* Heap JSON array of OpenAI tool objects for enabled rows; caller frees. */
char *capability_matrix_tools_json(const capability_matrix_t *m);

/* Human/model listing of enabled capabilities; caller frees. */
char *capability_matrix_prompt_listing(const capability_matrix_t *m);

/* Populate matrix from conf builtins + tools.commands. Clears m first. Returns 0 on success. */
int capability_matrix_build_from_config(capability_matrix_t *m, const agent_config_t *conf);

/* If n_calls > max_per_turn, print truncation warning to err. Returns 1 if warned. */
int capability_warn_tool_truncation(size_t n_calls, int max_per_turn, FILE *err);

#endif

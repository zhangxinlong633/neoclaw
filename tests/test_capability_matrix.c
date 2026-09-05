#include "agent_tools.h"
#include "capability_matrix.h"
#include "config.h"
#include "mcp_stdio.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char *json;
  char *listing;
  const cap_row_t *row;

  capability_matrix_init(&m);
  if (m.count != 0) {
    fprintf(stderr, "expected empty\n");
    return 1;
  }
  if (capability_matrix_find(&m, "read_file") != NULL) {
    fprintf(stderr, "find on empty should be NULL\n");
    return 1;
  }
  json = capability_matrix_tools_json(&m);
  if (!json || strcmp(json, "[]") != 0) {
    fprintf(stderr, "tools_json expected []\n");
    free(json);
    return 1;
  }
  free(json);
  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "none")) {
    fprintf(stderr, "listing expected none\n");
    free(listing);
    return 1;
  }
  free(listing);
  capability_matrix_free(&m);

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_commands_schema.json5") != 0) {
    fprintf(stderr, "load fixture failed\n");
    return 1;
  }
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    fprintf(stderr, "build_from_config failed\n");
    config_free(&conf);
    return 1;
  }
  if (!capability_matrix_find(&m, "read_file")) {
    fprintf(stderr, "missing read_file\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  if (!capability_matrix_find(&m, "grep")) {
    fprintf(stderr, "missing grep\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  if (capability_matrix_find(&m, "http_get")) {
    fprintf(stderr, "http_get should be absent when fetch disabled\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  row = capability_matrix_find(&m, "schema_echo");
  if (!row || !row->parameters_json || !strstr(row->parameters_json, "properties")) {
    fprintf(stderr, "schema_echo missing properties schema\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  json = capability_matrix_tools_json(&m);
  if (!json || !strstr(json, "schema_echo") || !strstr(json, "\"msg\"")) {
    fprintf(stderr, "tools_json missing schema_echo msg property: %s\n", json ? json : "(null)");
    free(json);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(json);
  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "schema_echo")) {
    fprintf(stderr, "listing missing schema_echo\n");
    free(listing);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(listing);

  {
    char *cap = NULL;
    size_t cap_sz = 0;
    FILE *mem = open_memstream(&cap, &cap_sz);
    int w;
    if (!mem) {
      fprintf(stderr, "open_memstream failed\n");
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
    w = capability_warn_tool_truncation(20, 16, mem);
    fclose(mem);
    if (!w || !cap || !strstr(cap, "truncated")) {
      fprintf(stderr, "truncation warn missing: %s\n", cap ? cap : "(null)");
      free(cap);
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
    free(cap);
    if (capability_warn_tool_truncation(3, 16, stderr) != 0) {
      fprintf(stderr, "unexpected warn for small n\n");
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
  }

  if (capability_matrix_find(&m, "run_command")) {
    fprintf(stderr, "run_command should be absent when shell_enabled is false\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }

  capability_matrix_free(&m);
  config_free(&conf);

  /* shell_enabled + run_command */
  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_commands_schema.json5") != 0) {
    fprintf(stderr, "reload schema fixture failed\n");
    return 1;
  }
  conf.tools.shell_enabled = 1;
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    fprintf(stderr, "build with shell failed\n");
    config_free(&conf);
    return 1;
  }
  if (!capability_matrix_find(&m, "run_command")) {
    fprintf(stderr, "missing run_command when shell_enabled\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  {
    char root[PATH_MAX];
    char *out = NULL;
    size_t out_len = 0;
    if (!realpath(".", root)) {
      fprintf(stderr, "realpath failed\n");
      return 1;
    }
    if (neo_dispatch_tool(&conf, root, "run_command",
                          "{\"argv\":[\"tests/fixtures/bin/echo-argv.sh\",\"hi\"]}", &out,
                          &out_len) != 0) {
      fprintf(stderr, "run_command dispatch failed\n");
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
    if (!out || !strstr(out, "hi")) {
      fprintf(stderr, "run_command missing hi: %s\n", out ? out : "(null)");
      free(out);
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
    free(out);
  }
  capability_matrix_free(&m);
  config_free(&conf);

  /* MCP stdio fixture */
  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_mcp_stdio.json5") != 0) {
    fprintf(stderr, "load mcp fixture failed\n");
    return 1;
  }
  if (conf.tools.mcp_server_count < 1) {
    fprintf(stderr, "expected mcp_servers\n");
    config_free(&conf);
    return 1;
  }
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    fprintf(stderr, "mcp matrix build failed\n");
    config_free(&conf);
    return 1;
  }
  if (!capability_matrix_find(&m, "mcp_mock_echo")) {
    fprintf(stderr, "missing mcp_mock_echo in matrix\n");
    capability_matrix_free(&m);
    config_free(&conf);
    mcp_stdio_shutdown_all();
    return 1;
  }
  {
    char root[PATH_MAX];
    char *out = NULL;
    size_t out_len = 0;
    if (!realpath(".", root)) {
      fprintf(stderr, "realpath failed\n");
      return 1;
    }
    if (neo_dispatch_tool(&conf, root, "mcp_mock_echo", "{\"text\":\"hi\"}", &out, &out_len) != 0) {
      fprintf(stderr, "dispatch mcp failed\n");
      capability_matrix_free(&m);
      config_free(&conf);
      mcp_stdio_shutdown_all();
      return 1;
    }
    if (!out || !strstr(out, "hi")) {
      fprintf(stderr, "mcp echo missing hi: %s\n", out ? out : "(null)");
      free(out);
      capability_matrix_free(&m);
      config_free(&conf);
      mcp_stdio_shutdown_all();
      return 1;
    }
    free(out);
  }
  capability_matrix_free(&m);
  config_free(&conf);
  mcp_stdio_shutdown_all();
  printf("ok\n");
  return 0;
}

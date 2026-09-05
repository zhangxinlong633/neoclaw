#include "command_tools.h"
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  agent_config_t c;
  char root_real[PATH_MAX];
  char *out = NULL;
  size_t out_len = 0;
  int idx;
  int rc;

  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/commands_min.json5") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (!realpath(".", root_real)) {
    fprintf(stderr, "realpath failed\n");
    config_free(&c);
    return 1;
  }
  idx = command_tool_find(&c, "echo_args");
  if (idx < 0) {
    fprintf(stderr, "find failed\n");
    config_free(&c);
    return 1;
  }
  rc = command_tool_run(&c, root_real, &c.tools.commands[idx], "{\"ping\":1}", &out, &out_len);
  if (rc != 0 || !out) {
    fprintf(stderr, "run failed rc=%d\n", rc);
    free(out);
    config_free(&c);
    return 1;
  }
  if (strstr(out, "name=echo_args") == NULL) {
    fprintf(stderr, "missing name in output:\n%s\n", out);
    free(out);
    config_free(&c);
    return 1;
  }
  if (strstr(out, "stdin={\"ping\":1}") == NULL) {
    fprintf(stderr, "missing stdin in output:\n%s\n", out);
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  config_free(&c);
  printf("ok\n");
  return 0;
}

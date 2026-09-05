#include "config.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  agent_config_t c;
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/commands_min.json") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (c.tools.command_count != 1) {
    fprintf(stderr, "want 1 command got %d\n", c.tools.command_count);
    config_free(&c);
    return 1;
  }
  if (!c.tools.commands[0].name || strcmp(c.tools.commands[0].name, "echo_args") != 0) {
    fprintf(stderr, "bad name\n");
    config_free(&c);
    return 1;
  }
  if (c.tools.commands[0].argv_count < 1) {
    fprintf(stderr, "empty argv\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);
  printf("ok\n");
  return 0;
}

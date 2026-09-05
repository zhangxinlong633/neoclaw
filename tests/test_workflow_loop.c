#include "config.h"
#include "workflow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  agent_config_t c;
  char *out = NULL;
  FILE *f;
  int lines = 0;
  char buf[64];

  unlink("tests/fixtures/count.out");
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/workflow_min.json") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (workflow_run(&c, "demo_loop", &out, 0) != 0) {
    fprintf(stderr, "workflow_run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  config_free(&c);

  f = fopen("tests/fixtures/count.out", "r");
  if (!f) {
    fprintf(stderr, "missing count.out\n");
    return 1;
  }
  while (fgets(buf, sizeof(buf), f)) lines++;
  fclose(f);
  if (lines != 2) {
    fprintf(stderr, "want 2 lines got %d\n", lines);
    return 1;
  }
  printf("ok\n");
  return 0;
}

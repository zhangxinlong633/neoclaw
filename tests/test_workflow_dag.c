#include "config.h"
#include "workflow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int count_lines(const char *path) {
  FILE *f = fopen(path, "r");
  int n = 0;
  char buf[64];
  if (!f) return -1;
  while (fgets(buf, sizeof(buf), f)) n++;
  fclose(f);
  return n;
}

int main(void) {
  agent_config_t c;
  char *out = NULL;
  const workflow_t *wf;

  unlink("tests/fixtures/count.out");
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/workflow_dag.json") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  wf = config_find_workflow(&c, "diamond");
  if (!wf || wf->step_count != 4 || wf->steps[1].depends_count != 1) {
    fprintf(stderr, "parse diamond bad\n");
    config_free(&c);
    return 1;
  }
  if (workflow_run(&c, "diamond", &out, 0) != 0) {
    fprintf(stderr, "diamond run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  if (count_lines("tests/fixtures/count.out") != 4) {
    fprintf(stderr, "diamond want 4 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  unlink("tests/fixtures/count.out");
  out = NULL;
  if (workflow_run(&c, "route_demo", &out, 0) != 0) {
    fprintf(stderr, "route run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  /* seed + take_then only */
  if (count_lines("tests/fixtures/count.out") != 2) {
    fprintf(stderr, "route want 2 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  unlink("tests/fixtures/count.out");
  out = NULL;
  if (workflow_run(&c, "route_merge", &out, 0) != 0) {
    fprintf(stderr, "route_merge run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  /* seed + join (fix_branch skipped on PASS) */
  if (count_lines("tests/fixtures/count.out") != 2) {
    fprintf(stderr, "route_merge want 2 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  config_free(&c);
  printf("ok\n");
  return 0;
}

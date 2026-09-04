#include "config.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  agent_config_t c;
  const workflow_t *wf;
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/workflow_min.yaml") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (c.workflow_count != 1) {
    fprintf(stderr, "want 1 workflow got %d\n", c.workflow_count);
    config_free(&c);
    return 1;
  }
  wf = config_find_workflow(&c, "demo_loop");
  if (!wf || wf->step_count != 2) {
    fprintf(stderr, "bad workflow steps=%d\n", wf ? wf->step_count : -1);
    config_free(&c);
    return 1;
  }
  if (wf->steps[0].type != WF_STEP_TOOL || !wf->steps[0].tool ||
      strcmp(wf->steps[0].tool, "count_run") != 0) {
    fprintf(stderr, "bad tool step\n");
    config_free(&c);
    return 1;
  }
  if (wf->steps[1].type != WF_STEP_LOOP || wf->steps[1].max_iters != 2 ||
      wf->steps[1].over_count != 1) {
    fprintf(stderr, "bad loop step\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);
  printf("ok\n");
  return 0;
}

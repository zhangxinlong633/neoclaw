#include "config.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  agent_config_t c;
  const dag_t *wf;
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/dag_min.json5") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (c.dag_count != 1) {
    fprintf(stderr, "want 1 workflow got %d\n", c.dag_count);
    config_free(&c);
    return 1;
  }
  wf = config_find_dag(&c, "demo_loop");
  if (!wf || wf->step_count != 2) {
    fprintf(stderr, "bad workflow steps=%d\n", wf ? wf->step_count : -1);
    config_free(&c);
    return 1;
  }
  if (wf->steps[0].type != DAG_STEP_TOOL || !wf->steps[0].tool ||
      strcmp(wf->steps[0].tool, "count_run") != 0) {
    fprintf(stderr, "bad tool step\n");
    config_free(&c);
    return 1;
  }
  if (wf->steps[1].type != DAG_STEP_LOOP || wf->steps[1].max_iters != 2 ||
      wf->steps[1].over_count != 1) {
    fprintf(stderr, "bad loop step\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);

  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/dag_tools_off.json5") != 0) {
    fprintf(stderr, "tools_off load failed\n");
    return 1;
  }
  wf = config_find_dag(&c, "llm_tools_off");
  if (!wf || wf->step_count != 2 || wf->steps[1].type != DAG_STEP_LLM ||
      !wf->steps[1].prompt || wf->steps[1].tools_on != 0) {
    fprintf(stderr, "tools_off step parse bad\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);

  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/dag_omit_type.json5") != 0) {
    fprintf(stderr, "omit_type load failed\n");
    return 1;
  }
  wf = config_find_dag(&c, "omit_type");
  if (!wf || wf->step_count != 1 || wf->steps[0].type != DAG_STEP_LLM ||
      !wf->steps[0].prompt) {
    fprintf(stderr, "omit_type coerce to llm failed\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);

  config_init(&c);
  if (config_load_file(&c, "not-a-config.yaml") == 0) {
    fprintf(stderr, "yaml should be rejected\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);

  printf("ok\n");
  return 0;
}

#include "plan.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int expect_contains(const char *hay, const char *needle, const char *label) {
  if (!hay || !strstr(hay, needle)) {
    fprintf(stderr, "FAIL %s: missing %s\n", label, needle);
    return 1;
  }
  return 0;
}

int main(void) {
  char *yaml = NULL;
  agent_config_t base, planned;
  char tmp[256];
  int fails = 0;
  const char *fenced =
      "Sure, here is the plan:\n"
      "```yaml\n"
      "workflows:\n"
      "  - name: planned\n"
      "    steps:\n"
      "      - id: a\n"
      "        type: llm\n"
      "        prompt: \"hello\"\n"
      "```\n"
      "Hope that helps.\n";
  const char *bare =
      "workflows:\n"
      "  - name: bare\n"
      "    steps:\n"
      "      - id: x\n"
      "        type: llm\n"
      "        prompt: \"x\"\n";

  if (plan_extract_workflows_yaml(fenced, &yaml) != 0) {
    fprintf(stderr, "extract fenced failed\n");
    return 1;
  }
  fails += expect_contains(yaml, "workflows:", "fenced");
  fails += expect_contains(yaml, "name: planned", "fenced");
  free(yaml);
  yaml = NULL;

  if (plan_extract_workflows_yaml(bare, &yaml) != 0) {
    fprintf(stderr, "extract bare failed\n");
    return 1;
  }
  fails += expect_contains(yaml, "name: bare", "bare");
  free(yaml);
  yaml = NULL;

  if (plan_extract_workflows_yaml("no yaml here", &yaml) == 0) {
    fprintf(stderr, "expected extract failure\n");
    free(yaml);
    return 1;
  }

  config_init(&base);
  base.model.base_url = strdup("http://127.0.0.1:9");
  base.model.name = strdup("test-model");
  base.model.api_key = strdup("k");
  base.model.max_tokens = 100;
  base.model.temperature = 0.0;
  base.tools.enabled = 0;
  base.tools.root = strdup(".");
  base.memory.path = strdup("MEMORY.md");
  base.memory.max_chars = 100;
  base.session_max_turns = 2;

  if (plan_materialize_config(&base, bare, &planned, tmp, sizeof(tmp)) != 0) {
    fprintf(stderr, "materialize failed\n");
    config_free(&base);
    return 1;
  }
  if (planned.workflow_count < 1 || !planned.workflows[0].name ||
      strcmp(planned.workflows[0].name, "bare") != 0) {
    fprintf(stderr, "materialize workflow name bad\n");
    fails++;
  }
  if (tmp[0]) unlink(tmp);
  config_free(&planned);
  config_free(&base);

  /* soft target_steps resolution + prompt text */
  if (plan_resolve_target_steps(NULL, 0) != PLAN_DEFAULT_TARGET_STEPS) {
    fprintf(stderr, "default target steps bad\n");
    fails++;
  }
  if (plan_resolve_target_steps(NULL, 15) != 15) {
    fprintf(stderr, "cli steps bad\n");
    fails++;
  }
  {
    agent_config_t pc;
    char *prompt;
    config_init(&pc);
    pc.plan.target_steps = 12;
    if (plan_resolve_target_steps(&pc, 0) != 12) {
      fprintf(stderr, "config target_steps bad\n");
      fails++;
    }
    if (plan_resolve_target_steps(&pc, 7) != 7) {
      fprintf(stderr, "cli overrides config bad\n");
      fails++;
    }
    prompt = plan_build_system_prompt(&pc, 10);
    if (!prompt || !strstr(prompt, "about 10 steps")) {
      fprintf(stderr, "prompt missing target steps\n");
      fails++;
    }
    if (!prompt || !strstr(prompt, "understand requirements") || !strstr(prompt, "at most 2 times")) {
      fprintf(stderr, "prompt missing team pipeline guidance\n");
      fails++;
    }
    free(prompt);
    config_free(&pc);
  }

  if (fails) {
    fprintf(stderr, "%d failures\n", fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}

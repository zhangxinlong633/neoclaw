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
  char *json = NULL;
  agent_config_t base, planned;
  char tmp[256];
  int fails = 0;
  const char *fenced =
      "Sure:\n```json\n"
      "{\"workflows\":[{\"name\":\"planned\",\"steps\":["
      "{\"id\":\"a\",\"type\":\"llm\",\"prompt\":\"hello\"}]}]}"
      "\n```\n";
  const char *bare =
      "{\"workflows\":[{\"name\":\"bare\",\"steps\":["
      "{\"id\":\"x\",\"type\":\"llm\",\"prompt\":\"x\"}]}]}";

  if (plan_extract_workflows_json(fenced, &json) != 0) {
    fprintf(stderr, "extract fenced failed\n");
    return 1;
  }
  fails += expect_contains(json, "\"workflows\"", "fenced");
  fails += expect_contains(json, "planned", "fenced");
  free(json);
  json = NULL;

  if (plan_extract_workflows_json(bare, &json) != 0) {
    fprintf(stderr, "extract bare failed\n");
    return 1;
  }
  fails += expect_contains(json, "bare", "bare");
  free(json);
  json = NULL;

  if (plan_extract_workflows_json("no json here", &json) == 0) {
    fprintf(stderr, "expected extract failure\n");
    free(json);
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
    fprintf(stderr, "bad materialize name\n");
    config_free(&planned);
    config_free(&base);
    if (tmp[0]) unlink(tmp);
    return 1;
  }
  config_free(&planned);
  if (tmp[0]) unlink(tmp);

  {
    char *prompt = plan_build_system_prompt(&base, 10);
    if (!prompt || !strstr(prompt, "JSON") || !strstr(prompt, "10")) {
      fprintf(stderr, "prompt missing JSON/target\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    free(prompt);
  }

  config_free(&base);
  if (fails) return 1;
  printf("ok\n");
  return 0;
}

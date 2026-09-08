#include "dag.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_eq(int got, int want, const char *label) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %d want %d\n", label, got, want);
    return 1;
  }
  return 0;
}

int main(void) {
  int fails = 0;
  agent_config_t c;

  fails += expect_eq(dag_parse_fail_llm_reply(NULL), 0, "null");
  fails += expect_eq(dag_parse_fail_llm_reply(""), 0, "empty");
  fails += expect_eq(dag_parse_fail_llm_reply("please RETRY now"), 1, "retry word");
  fails += expect_eq(dag_parse_fail_llm_reply("RETRY"), 1, "retry alone");
  fails += expect_eq(dag_parse_fail_llm_reply("reason\nABORT\n"), 0, "abort");
  fails += expect_eq(dag_parse_fail_llm_reply("I would retry then ABORT"), 0, "abort wins");
  fails += expect_eq(dag_parse_fail_llm_reply("maybe later"), 0, "unclear");
  fails += expect_eq(dag_parse_fail_llm_reply("unretryable"), 0, "not word boundary");

  config_init(&c);
  if (c.dag_runtime.on_tool_fail_llm != 0 || c.dag_runtime.on_tool_fail_max_calls != 1) {
    fprintf(stderr, "FAIL defaults llm=%d max=%d\n", c.dag_runtime.on_tool_fail_llm,
            c.dag_runtime.on_tool_fail_max_calls);
    fails++;
  }
  if (config_load_file(&c, "tests/fixtures/dag_on_fail.json5") != 0) {
    fprintf(stderr, "FAIL load dag_on_fail.json5\n");
    config_free(&c);
    return 1;
  }
  fails += expect_eq(c.dag_runtime.on_tool_fail_llm, 1, "llm on");
  fails += expect_eq(c.dag_runtime.on_tool_fail_max_calls, 2, "max_calls");
  config_free(&c);

  if (fails) {
    fprintf(stderr, "%d failures\n", fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}

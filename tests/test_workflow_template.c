#include "workflow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  const char *ids[] = {"fetch"};
  const char *texts[] = {"hello"};
  char *out = workflow_expand_template("X {{prev}} Y {{steps.fetch}} Z", "P", ids, texts, 1);
  if (!out) {
    fprintf(stderr, "expand null\n");
    return 1;
  }
  if (strcmp(out, "X P Y hello Z") != 0) {
    fprintf(stderr, "got [%s]\n", out);
    free(out);
    return 1;
  }
  free(out);
  out = workflow_expand_template("bad {{steps.missing}}", "", ids, texts, 1);
  if (out) {
    fprintf(stderr, "should fail unknown step\n");
    free(out);
    return 1;
  }
  printf("ok\n");
  return 0;
}

#include "dag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  const char *ids[] = {"fetch"};
  const char *texts[] = {"hello"};
  char *out = dag_expand_template("X {{prev}} Y {{steps.fetch}} Z", "P", ids, texts, 1);
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
  out = dag_expand_template("bad {{steps.missing}} end", "", ids, texts, 1);
  if (!out || strcmp(out, "bad  end") != 0) {
    fprintf(stderr, "missing step should expand empty, got [%s]\n", out ? out : "null");
    free(out);
    return 1;
  }
  free(out);
  printf("ok\n");
  return 0;
}

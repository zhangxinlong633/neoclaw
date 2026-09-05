#include "capability_matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  capability_matrix_t m;
  char *json;
  char *listing;

  capability_matrix_init(&m);
  if (m.count != 0) {
    fprintf(stderr, "expected empty\n");
    return 1;
  }
  if (capability_matrix_find(&m, "read_file") != NULL) {
    fprintf(stderr, "find on empty should be NULL\n");
    return 1;
  }
  json = capability_matrix_tools_json(&m);
  if (!json || strcmp(json, "[]") != 0) {
    fprintf(stderr, "tools_json expected []\n");
    free(json);
    return 1;
  }
  free(json);
  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "none")) {
    fprintf(stderr, "listing expected none\n");
    free(listing);
    return 1;
  }
  free(listing);
  capability_matrix_free(&m);
  printf("ok\n");
  return 0;
}

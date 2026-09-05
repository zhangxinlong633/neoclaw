#include "capability_matrix.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char *json;
  char *listing;
  const cap_row_t *row;

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

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_commands_schema.json5") != 0) {
    fprintf(stderr, "load fixture failed\n");
    return 1;
  }
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    fprintf(stderr, "build_from_config failed\n");
    config_free(&conf);
    return 1;
  }
  if (!capability_matrix_find(&m, "read_file")) {
    fprintf(stderr, "missing read_file\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  if (capability_matrix_find(&m, "http_get")) {
    fprintf(stderr, "http_get should be absent when fetch disabled\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  row = capability_matrix_find(&m, "schema_echo");
  if (!row || !row->parameters_json || !strstr(row->parameters_json, "properties")) {
    fprintf(stderr, "schema_echo missing properties schema\n");
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  json = capability_matrix_tools_json(&m);
  if (!json || !strstr(json, "schema_echo") || !strstr(json, "\"msg\"")) {
    fprintf(stderr, "tools_json missing schema_echo msg property: %s\n", json ? json : "(null)");
    free(json);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(json);
  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "schema_echo")) {
    fprintf(stderr, "listing missing schema_echo\n");
    free(listing);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(listing);
  capability_matrix_free(&m);
  config_free(&conf);
  printf("ok\n");
  return 0;
}

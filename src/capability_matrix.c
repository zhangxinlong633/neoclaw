#include "capability_matrix.h"
#include <stdlib.h>
#include <string.h>

void capability_matrix_init(capability_matrix_t *m) {
  if (!m) return;
  m->rows = NULL;
  m->count = 0;
  m->cap = 0;
}

static void free_row(cap_row_t *r) {
  if (!r) return;
  free(r->name);
  free(r->source_detail);
  free(r->description);
  free(r->parameters_json);
  free(r->mcp_server);
  free(r->mcp_tool);
  memset(r, 0, sizeof(*r));
}

void capability_matrix_free(capability_matrix_t *m) {
  int i;
  if (!m) return;
  for (i = 0; i < m->count; i++) free_row(&m->rows[i]);
  free(m->rows);
  m->rows = NULL;
  m->count = 0;
  m->cap = 0;
}

const cap_row_t *capability_matrix_find(const capability_matrix_t *m, const char *name) {
  int i;
  if (!m || !name) return NULL;
  for (i = 0; i < m->count; i++) {
    if (m->rows[i].name && strcmp(m->rows[i].name, name) == 0) return &m->rows[i];
  }
  return NULL;
}

char *capability_matrix_tools_json(const capability_matrix_t *m) {
  (void)m;
  return strdup("[]");
}

char *capability_matrix_prompt_listing(const capability_matrix_t *m) {
  (void)m;
  return strdup("(none)\n");
}

#include "capability_matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *DEFAULT_PARAMS = "{\"type\":\"object\"}";

static const char *PARAMS_READ =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":"
    "\"Relative path under workspace root\"}},\"required\":[\"path\"]}";
static const char *PARAMS_WRITE =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},"
    "\"required\":[\"path\",\"content\"]}";
static const char *PARAMS_LIST =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":"
    "\"Relative directory; use . for workspace root\"}},\"required\":[\"path\"]}";
static const char *PARAMS_HTTP =
    "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}";

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
    if (m->rows[i].enabled && m->rows[i].name && strcmp(m->rows[i].name, name) == 0)
      return &m->rows[i];
  }
  return NULL;
}

static char *dup_s(const char *s) {
  size_t n;
  char *p;
  if (!s) return NULL;
  n = strlen(s) + 1;
  p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

static int ensure_cap(capability_matrix_t *m) {
  cap_row_t *np;
  int ncap;
  if (m->count < m->cap) return 0;
  ncap = m->cap ? m->cap * 2 : 16;
  np = realloc(m->rows, (size_t)ncap * sizeof(cap_row_t));
  if (!np) return -1;
  m->rows = np;
  m->cap = ncap;
  return 0;
}

static int add_row(capability_matrix_t *m, const char *name, cap_source_t src, const char *desc,
                   const char *params, cap_effect_t effect, int builtin_id,
                   const tool_command_t *cmd) {
  cap_row_t *r;
  if (ensure_cap(m) != 0) return -1;
  r = &m->rows[m->count];
  memset(r, 0, sizeof(*r));
  r->name = dup_s(name);
  r->description = dup_s(desc ? desc : name);
  r->parameters_json = dup_s(params && params[0] ? params : DEFAULT_PARAMS);
  r->source = src;
  r->effect = effect;
  r->enabled = 1;
  r->builtin_id = builtin_id;
  r->command = cmd;
  if (cmd) {
    r->timeout_sec = cmd->timeout_sec;
    r->max_output_bytes = cmd->max_output_bytes;
  }
  if (!r->name || !r->description || !r->parameters_json) {
    free_row(r);
    return -1;
  }
  m->count++;
  return 0;
}

int capability_matrix_build_from_config(capability_matrix_t *m, const agent_config_t *conf) {
  int i;
  if (!m) return -1;
  capability_matrix_free(m);
  capability_matrix_init(m);
  if (!conf || !conf->tools.enabled) return 0;

  if (add_row(m, "read_file", CAP_SRC_BUILTIN,
              "Read a UTF-8 text file under the workspace root.", PARAMS_READ, CAP_EFFECT_READ,
              CAP_BUILTIN_READ_FILE, NULL) != 0)
    return -1;
  if (add_row(m, "write_file", CAP_SRC_BUILTIN,
              "Write content to a file under the workspace root (create/overwrite).", PARAMS_WRITE,
              CAP_EFFECT_WRITE, CAP_BUILTIN_WRITE_FILE, NULL) != 0)
    return -1;
  if (add_row(m, "list_dir", CAP_SRC_BUILTIN,
              "List names of files and subdirectories at a path under the workspace root (non-recursive).",
              PARAMS_LIST, CAP_EFFECT_READ, CAP_BUILTIN_LIST_DIR, NULL) != 0)
    return -1;

  if (conf->tools.http_fetch_enabled && conf->tools.http_allow_hosts &&
      conf->tools.http_allow_hosts[0]) {
    if (add_row(m, "http_get", CAP_SRC_BUILTIN,
                "HTTPS GET for allowlisted hosts only. No redirects. Body truncated.", PARAMS_HTTP,
                CAP_EFFECT_NETWORK, CAP_BUILTIN_HTTP_GET, NULL) != 0)
      return -1;
  }

  for (i = 0; i < conf->tools.command_count; i++) {
    const tool_command_t *cmd = &conf->tools.commands[i];
    const char *desc;
    const char *params;
    if (!cmd->name || !cmd->name[0]) continue;
    if (capability_matrix_find(m, cmd->name)) {
      fprintf(stderr, "neo: capability matrix: skip duplicate name '%s'\n", cmd->name);
      continue;
    }
    desc = (cmd->description && cmd->description[0]) ? cmd->description : cmd->name;
    params = (cmd->parameters_json && cmd->parameters_json[0]) ? cmd->parameters_json : DEFAULT_PARAMS;
    if (add_row(m, cmd->name, CAP_SRC_COMMAND, desc, params, CAP_EFFECT_EXEC, CAP_BUILTIN_NONE,
                cmd) != 0)
      return -1;
  }
  return 0;
}

static int buf_grow(char **buf, size_t *cap, size_t need) {
  char *nbuf;
  size_t ncap = *cap ? *cap : 256;
  while (ncap < need) ncap *= 2;
  if (ncap == *cap) return 0;
  nbuf = realloc(*buf, ncap);
  if (!nbuf) return -1;
  *buf = nbuf;
  *cap = ncap;
  return 0;
}

static int buf_append(char **buf, size_t *len, size_t *cap, const char *s) {
  size_t n;
  if (!s) s = "";
  n = strlen(s);
  if (buf_grow(buf, cap, *len + n + 1) != 0) return -1;
  memcpy(*buf + *len, s, n + 1);
  *len += n;
  return 0;
}

static int buf_append_json_str(char **buf, size_t *len, size_t *cap, const char *s) {
  size_t i;
  if (buf_append(buf, len, cap, "\"") != 0) return -1;
  if (!s) s = "";
  for (i = 0; s[i]; i++) {
    char tmp[8];
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') {
      tmp[0] = '\\';
      tmp[1] = (char)c;
      tmp[2] = '\0';
      if (buf_append(buf, len, cap, tmp) != 0) return -1;
    } else if (c < 0x20) {
      snprintf(tmp, sizeof(tmp), "\\u%04x", c);
      if (buf_append(buf, len, cap, tmp) != 0) return -1;
    } else {
      tmp[0] = (char)c;
      tmp[1] = '\0';
      if (buf_append(buf, len, cap, tmp) != 0) return -1;
    }
  }
  return buf_append(buf, len, cap, "\"");
}

char *capability_matrix_tools_json(const capability_matrix_t *m) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  int i, first = 1;
  if (buf_append(&buf, &len, &cap, "[") != 0) {
    free(buf);
    return NULL;
  }
  if (m) {
    for (i = 0; i < m->count; i++) {
      const cap_row_t *r = &m->rows[i];
      if (!r->enabled || !r->name) continue;
      if (!first && buf_append(&buf, &len, &cap, ",") != 0) {
        free(buf);
        return NULL;
      }
      first = 0;
      if (buf_append(&buf, &len, &cap, "{\"type\":\"function\",\"function\":{\"name\":") != 0 ||
          buf_append_json_str(&buf, &len, &cap, r->name) != 0 ||
          buf_append(&buf, &len, &cap, ",\"description\":") != 0 ||
          buf_append_json_str(&buf, &len, &cap, r->description ? r->description : "") != 0 ||
          buf_append(&buf, &len, &cap, ",\"parameters\":") != 0 ||
          buf_append(&buf, &len, &cap,
                     r->parameters_json && r->parameters_json[0] ? r->parameters_json : DEFAULT_PARAMS) !=
              0 ||
          buf_append(&buf, &len, &cap, "}}") != 0) {
        free(buf);
        return NULL;
      }
    }
  }
  if (buf_append(&buf, &len, &cap, "]") != 0) {
    free(buf);
    return NULL;
  }
  return buf;
}

static const char *src_label(cap_source_t s) {
  switch (s) {
    case CAP_SRC_BUILTIN: return "builtin";
    case CAP_SRC_COMMAND: return "command";
    case CAP_SRC_MCP: return "mcp";
    default: return "?";
  }
}

static const char *effect_label(cap_effect_t e) {
  switch (e) {
    case CAP_EFFECT_READ: return "read";
    case CAP_EFFECT_WRITE: return "write";
    case CAP_EFFECT_EXEC: return "exec";
    case CAP_EFFECT_NETWORK: return "network";
    default: return "?";
  }
}

char *capability_matrix_prompt_listing(const capability_matrix_t *m) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  int i, any = 0;
  char line[1024];
  if (!m || m->count == 0) return dup_s("(none)\n");
  for (i = 0; i < m->count; i++) {
    const cap_row_t *r = &m->rows[i];
    if (!r->enabled || !r->name) continue;
    any = 1;
    snprintf(line, sizeof(line), "- %s (%s, %s): %s\n", r->name, src_label(r->source),
             effect_label(r->effect), r->description ? r->description : "");
    if (buf_append(&buf, &len, &cap, line) != 0) {
      free(buf);
      return NULL;
    }
  }
  if (!any) {
    free(buf);
    return dup_s("(none)\n");
  }
  return buf;
}

/*
 * OpenAI-style tool_calls: read_file, write_file, list_dir; optional http_get (allowlist).
 */
#include "agent_tools.h"
#include "capability_matrix.h"
#include "command_tools.h"
#include "yyjson.h"
#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define NEO_MAX_TOOLS_PER_TURN 16
#define NEO_BODY_INIT (256 * 1024)

typedef struct {
  char *s;
  size_t len;
  size_t cap;
} NeoBuf;

typedef struct {
  char *id;
  char *name;
  char *arguments; /* JSON object as string */
} NeoToolCall;

static void neo_buf_free(NeoBuf *b) {
  free(b->s);
  b->s = NULL;
  b->len = b->cap = 0;
}

static int neo_buf_reserve(NeoBuf *b, size_t add) {
  size_t need = b->len + add + 1;
  if (need <= b->cap) return 0;
  size_t nc = b->cap ? b->cap : NEO_BODY_INIT;
  while (nc < need) {
    if (nc > (SIZE_MAX / 2)) return -1;
    nc *= 2;
  }
  char *p = realloc(b->s, nc);
  if (!p) return -1;
  b->s = p;
  b->cap = nc;
  return 0;
}

static int neo_buf_append(NeoBuf *b, const char *frag, size_t n) {
  if (!frag) return 0;
  if (n == 0) n = strlen(frag);
  if (neo_buf_reserve(b, n) != 0) return -1;
  memcpy(b->s + b->len, frag, n);
  b->len += n;
  b->s[b->len] = '\0';
  return 0;
}

static void neo_json_escape(const char *in, NeoBuf *b) {
  if (!in) return;
  for (; *in; in++) {
    char esc[8];
    const char *e = NULL;
    if (*in == '\\' || *in == '"' || *in == '\n' || *in == '\r' || *in == '\t') {
      esc[0] = '\\';
      esc[1] = (*in == '\n') ? 'n' : (*in == '\r') ? 'r' : (*in == '\t') ? 't' : *in;
      esc[2] = '\0';
      e = esc;
    } else {
      esc[0] = *in;
      esc[1] = '\0';
      e = esc;
    }
    neo_buf_append(b, e, strlen(e));
  }
}

static int neo_build_tools_json(NeoBuf *b, const agent_config_t *conf) {
  capability_matrix_t m;
  char *json;
  int r;
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, conf) != 0) {
    capability_matrix_free(&m);
    return -1;
  }
  json = capability_matrix_tools_json(&m);
  capability_matrix_free(&m);
  if (!json) return -1;
  r = neo_buf_append(b, json, 0);
  free(json);
  return r;
}

static char *yy_strdup_val(yyjson_val *v) {
  const char *s;
  size_t n;
  char *o;
  if (!yyjson_is_str(v)) return NULL;
  s = yyjson_get_str(v);
  n = yyjson_get_len(v);
  o = malloc(n + 1);
  if (!o) return NULL;
  memcpy(o, s ? s : "", n);
  o[n] = '\0';
  return o;
}

static int extract_string_field(const char *obj, const char *key, char *out, size_t out_cap) {
  yyjson_doc *doc;
  yyjson_val *root, *v;
  const char *s;
  size_t n;
  if (!obj || !key || !out || out_cap == 0) return -1;
  doc = yyjson_read(obj, strlen(obj), 0);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  v = yyjson_obj_get(root, key);
  if (!yyjson_is_str(v)) {
    yyjson_doc_free(doc);
    return -1;
  }
  s = yyjson_get_str(v);
  n = yyjson_get_len(v);
  if (n >= out_cap) {
    yyjson_doc_free(doc);
    return -1;
  }
  memcpy(out, s ? s : "", n);
  out[n] = '\0';
  yyjson_doc_free(doc);
  return 0;
}

static yyjson_val *choice0_message(yyjson_val *root) {
  yyjson_val *choices, *c0, *msg;
  choices = yyjson_obj_get(root, "choices");
  if (!yyjson_is_arr(choices) || yyjson_arr_size(choices) < 1) return NULL;
  c0 = yyjson_arr_get(choices, 0);
  msg = yyjson_obj_get(c0, "message");
  return yyjson_is_obj(msg) ? msg : NULL;
}

static int msg_content_to_llm(yyjson_val *msg, llm_response_t *out) {
  yyjson_val *content;
  out->data = NULL;
  out->size = 0;
  if (!msg) return -1;
  content = yyjson_obj_get(msg, "content");
  if (!content || yyjson_is_null(content)) {
    out->data = malloc(1);
    if (!out->data) return -1;
    out->data[0] = '\0';
    out->size = 0;
    return 0;
  }
  if (!yyjson_is_str(content)) return -1;
  out->data = yy_strdup_val(content);
  if (!out->data) return -1;
  out->size = strlen(out->data);
  return 0;
}

static void neo_tool_calls_free(NeoToolCall *tc, int n) {
  int i;
  for (i = 0; i < n; i++) {
    free(tc[i].id);
    free(tc[i].name);
    free(tc[i].arguments);
  }
}

static int parse_tool_calls_yy(yyjson_val *msg, NeoToolCall *out, int *out_n) {
  yyjson_val *tc, *el;
  size_t i, n;
  *out_n = 0;
  if (!msg) return -1;
  tc = yyjson_obj_get(msg, "tool_calls");
  if (!yyjson_is_arr(tc)) return -1;
  n = yyjson_arr_size(tc);
  for (i = 0; i < n && *out_n < NEO_MAX_TOOLS_PER_TURN; i++) {
    NeoToolCall *t;
    yyjson_val *fn, *args;
    el = yyjson_arr_get(tc, i);
    if (!yyjson_is_obj(el)) return -1;
    t = &out[*out_n];
    t->id = t->name = t->arguments = NULL;
    t->id = yy_strdup_val(yyjson_obj_get(el, "id"));
    fn = yyjson_obj_get(el, "function");
    if (yyjson_is_obj(fn)) {
      t->name = yy_strdup_val(yyjson_obj_get(fn, "name"));
      args = yyjson_obj_get(fn, "arguments");
      if (yyjson_is_str(args)) {
        t->arguments = yy_strdup_val(args);
      } else if (yyjson_is_obj(args) || yyjson_is_arr(args)) {
        t->arguments = yyjson_val_write(args, 0, NULL);
      }
    }
    if (t->id && t->name && t->arguments) {
      (*out_n)++;
    } else {
      free(t->id);
      free(t->name);
      free(t->arguments);
      t->id = t->name = t->arguments = NULL;
    }
  }
  return 0;
}

static int append_message_json(yyjson_val *msg, NeoBuf *dst) {
  char *s = yyjson_val_write(msg, 0, NULL);
  int r;
  if (!s) return -1;
  r = neo_buf_append(dst, s, strlen(s));
  free(s);
  return r;
}

#if defined(__APPLE__) || defined(__linux__)
static int path_is_safe_rel(const char *rel) {
  if (!rel || !rel[0] || rel[0] == '/') return 0;
  if (strstr(rel, "..")) return 0;
  return 1;
}

static int resolve_under_root(const char *root_real, const char *rel, char *full, size_t full_cap) {
  if (!path_is_safe_rel(rel)) return -1;
  if (snprintf(full, full_cap, "%s/%s", root_real, rel) >= (int)full_cap) return -1;
  char resolved[PATH_MAX];
  if (realpath(full, resolved)) {
    size_t lr = strlen(root_real);
    if (strncmp(resolved, root_real, lr) != 0) return -1;
    if (resolved[lr] != '\0' && resolved[lr] != '/') return -1;
    strncpy(full, resolved, full_cap - 1);
    full[full_cap - 1] = '\0';
    return 0;
  }
  /* New file: ensure parent directory is under root */
  char relcopy[PATH_MAX];
  if (strlen(rel) >= sizeof(relcopy)) return -1;
  strcpy(relcopy, rel);
  char *slash = strrchr(relcopy, '/');
  if (!slash) {
    strncpy(full, root_real, full_cap - 1);
    full[full_cap - 1] = '\0';
    strncat(full, "/", full_cap - strlen(full) - 1);
    strncat(full, rel, full_cap - strlen(full) - 1);
    return 0;
  }
  *slash = '\0';
  char parent_full[PATH_MAX];
  if (snprintf(parent_full, sizeof(parent_full), "%s/%s", root_real, relcopy) >= (int)sizeof(parent_full)) return -1;
  if (!realpath(parent_full, resolved)) return -1;
  size_t lr = strlen(root_real);
  if (strncmp(resolved, root_real, lr) != 0) return -1;
  if (resolved[lr] != '\0' && resolved[lr] != '/') return -1;
  if (snprintf(full, full_cap, "%s/%s", root_real, rel) >= (int)full_cap) return -1;
  return 0;
}
#else
static int resolve_under_root(const char *root_real, const char *rel, char *full, size_t full_cap) {
  (void)root_real;
  (void)rel;
  (void)full;
  (void)full_cap;
  return -1;
}
#endif

static int neo_host_eq_ci(const char *a, const char *b) {
  for (;;) {
    unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return 0;
    if (ca == '\0') return 1;
    a++;
    b++;
  }
}

static int http_host_in_allowlist(const char *host, const char *csv) {
  char buf[512];
  char *saveptr = NULL;
  size_t n = strlen(csv);
  if (n == 0 || n >= sizeof(buf)) return 0;
  memcpy(buf, csv, n + 1);
  for (char *tok = strtok_r(buf, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr)) {
    while (*tok == ' ' || *tok == '\t') tok++;
    {
      char *e = tok + strlen(tok);
      while (e > tok && (e[-1] == ' ' || e[-1] == '\t')) {
        e--;
        *e = '\0';
      }
    }
    if (*tok != '\0' && neo_host_eq_ci(tok, host)) return 1;
  }
  return 0;
}

static int url_https_extract_host(const char *url, char *host, size_t hostcap) {
  const char *p;
  const char *at;
  const char *slash;
  const char *end;
  size_t n;
  if (strncmp(url, "https://", 8) != 0) return -1;
  p = url + 8;
  at = strchr(p, '@');
  slash = strchr(p, '/');
  if (at != NULL && (slash == NULL || at < slash)) p = at + 1;
  end = p;
  while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') end++;
  n = (size_t)(end - p);
  if (n == 0 || n >= hostcap) return -1;
  memcpy(host, p, n);
  host[n] = '\0';
  {
    char *c = strchr(host, ':');
    if (c) *c = '\0';
  }
  if (host[0] == '[') return -1;
  return 0;
}

struct neohttp_write_ctx {
  NeoBuf *buf;
  size_t max;
  int truncated;
};

static size_t neohttp_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  struct neohttp_write_ctx *w = (struct neohttp_write_ctx *)userdata;
  size_t add = size * nmemb;
  size_t room = w->max > w->buf->len ? w->max - w->buf->len : 0;
  if (add > room) {
    w->truncated = 1;
    add = room;
  }
  if (add > 0 && neo_buf_append(w->buf, ptr, add) != 0) return 0;
  return size * nmemb;
}

static int tool_http_get(const agent_config_t *conf, const char *args_json, NeoBuf *result) {
  char urlbuf[2048];
  char host[256];
  NeoBuf out = {0};
  struct neohttp_write_ctx w;
  CURL *curl;
  CURLcode cr;
  long code = 0;
  if (!conf->tools.http_fetch_enabled || conf->tools.http_allow_hosts == NULL || conf->tools.http_allow_hosts[0] == '\0')
    return neo_buf_append(result, "ERROR: http_get disabled (set http_fetch_enabled: true and http_allow_hosts)", 0);
  if (extract_string_field(args_json, "url", urlbuf, sizeof(urlbuf)) != 0)
    return neo_buf_append(result, "ERROR: missing url", 0);
  if (url_https_extract_host(urlbuf, host, sizeof(host)) != 0)
    return neo_buf_append(result, "ERROR: url must be https://hostname/... (no IPv6 literal)", 0);
  if (!http_host_in_allowlist(host, conf->tools.http_allow_hosts))
    return neo_buf_append(result, "ERROR: host not in tools.http_allow_hosts", 0);

  w.buf = &out;
  w.max = (size_t)(conf->tools.http_fetch_max_bytes > 0 ? conf->tools.http_fetch_max_bytes : 262144);
  w.truncated = 0;

  curl = curl_easy_init();
  if (!curl) return neo_buf_append(result, "ERROR: curl init failed", 0);
  curl_easy_setopt(curl, CURLOPT_URL, urlbuf);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, neohttp_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &w);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "neo-tools/1");
  cr = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  if (cr != CURLE_OK) {
    neo_buf_free(&out);
    neo_buf_append(result, "ERROR: ", 0);
    neo_json_escape(curl_easy_strerror(cr), result);
    return 0;
  }
  neo_buf_append(result, "HTTP ", 0);
  {
    char nb[40];
    snprintf(nb, sizeof(nb), "%ld\n", code);
    neo_buf_append(result, nb, strlen(nb));
  }
  neo_buf_append(result, out.s ? out.s : "", out.len);
  if (w.truncated) neo_buf_append(result, "\n...[truncated]", 0);
  neo_buf_free(&out);
  return 0;
}

#if defined(__APPLE__) || defined(__linux__)
static int tool_list_dir(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  char rel[PATH_MAX];
  char full[PATH_MAX];
  struct stat st;
  int cap;
  int n;
  DIR *d;
  struct dirent *e;
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
    return neo_buf_append(result, "ERROR: missing path", 0);
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0)
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
    return neo_buf_append(result, "ERROR: not a directory", 0);
  cap = conf->tools.list_dir_max_entries > 0 ? conf->tools.list_dir_max_entries : 256;
  d = opendir(full);
  if (!d) {
    neo_buf_append(result, "ERROR: opendir: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  n = 0;
  while ((e = readdir(d)) != NULL && n < cap) {
    if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
      continue;
    if (neo_buf_append(result, e->d_name, strlen(e->d_name)) != 0) {
      closedir(d);
      return -1;
    }
    if (neo_buf_append(result, "\n", 1) != 0) {
      closedir(d);
      return -1;
    }
    n++;
  }
  closedir(d);
  return 0;
}
#else
static int tool_list_dir(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  (void)conf;
  (void)root_real;
  (void)args_json;
  return neo_buf_append(result, "ERROR: list_dir unsupported on this platform", 0);
}
#endif

static int tool_read_file(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  char rel[PATH_MAX];
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
    return neo_buf_append(result, "ERROR: missing path", 0);
  char full[PATH_MAX];
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0)
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  FILE *f = fopen(full, "rb");
  if (!f) {
    neo_buf_append(result, "ERROR: open failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  int cap = conf->tools.max_read_bytes > 0 ? conf->tools.max_read_bytes : 262144;
  if (sz > cap) {
    fclose(f);
    return neo_buf_append(result, "ERROR: file too large", 0);
  }
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return neo_buf_append(result, "ERROR: oom", 0);
  }
  size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = '\0';
  if (neo_buf_append(result, buf, n) != 0) {
    free(buf);
    return -1;
  }
  free(buf);
  return 0;
}

static int tool_write_file(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  (void)conf;
  char rel[PATH_MAX];
  size_t ccap = 512 * 1024;
  char *content = malloc(ccap);
  if (!content) return neo_buf_append(result, "ERROR: oom", 0);
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing path", 0);
  }
  if (extract_string_field(args_json, "content", content, ccap) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing content", 0);
  }
  char full[PATH_MAX];
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  }
  FILE *f = fopen(full, "wb");
  if (!f) {
    free(content);
    neo_buf_append(result, "ERROR: write failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  size_t L = strlen(content);
  if (fwrite(content, 1, L, f) != L) {
    fclose(f);
    free(content);
    return neo_buf_append(result, "ERROR: short write", 0);
  }
  fclose(f);
  free(content);
  return neo_buf_append(result, "ok", 0);
}

static int run_one_tool(const agent_config_t *conf, const char *root_real, NeoToolCall *tc, NeoBuf *result) {
  int idx;
  char *out = NULL;
  size_t out_len = 0;
  result->len = 0;
  if (result->s) result->s[0] = '\0';
  fprintf(stderr, "neo tool: %s\n", tc->name);
  if (strcmp(tc->name, "read_file") == 0)
    return tool_read_file(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "write_file") == 0)
    return tool_write_file(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "list_dir") == 0)
    return tool_list_dir(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "http_get") == 0)
    return tool_http_get(conf, tc->arguments, result);
  idx = command_tool_find(conf, tc->name);
  if (idx >= 0) {
    if (command_tool_run(conf, root_real, &conf->tools.commands[idx],
                         tc->arguments ? tc->arguments : "{}", &out, &out_len) != 0) {
      int r = neo_buf_append(result, out ? out : "ERROR: command tool failed", 0);
      free(out);
      return r;
    }
    {
      int r = neo_buf_append(result, out ? out : "", 0);
      free(out);
      return r;
    }
  }
  return neo_buf_append(result, "ERROR: unknown tool", 0);
}

int neo_dispatch_tool(const agent_config_t *conf, const char *root_real,
                      const char *name, const char *args_json,
                      char **out_text, size_t *out_len) {
  NeoToolCall tc;
  NeoBuf result;
  int r;
  if (out_text) *out_text = NULL;
  if (out_len) *out_len = 0;
  if (!conf || !root_real || !name || !out_text) return -1;
  memset(&tc, 0, sizeof(tc));
  memset(&result, 0, sizeof(result));
  tc.name = (char *)name;
  tc.arguments = (char *)(args_json ? args_json : "{}");
  r = run_one_tool(conf, root_real, &tc, &result);
  if (r != 0) {
    neo_buf_free(&result);
    return -1;
  }
  *out_text = result.s ? result.s : strdup("");
  if (out_len) *out_len = result.len;
  /* ownership transferred */
  return 0;
}

int agent_run_with_tools(
  const agent_config_t *conf,
  const char *system_prompt,
  const llm_message_t *prefix_messages,
  int n_prefix,
  const char *user_message,
  llm_response_t *out_text) {
  out_text->data = NULL;
  out_text->size = 0;
  if (!conf || !conf->model.base_url || !system_prompt || !user_message) return -1;

#if !defined(__APPLE__) && !defined(__linux__)
  (void)prefix_messages;
  (void)n_prefix;
  fprintf(stderr, "neo: tools not supported on this platform\n");
  return -1;
#else
  char root_real[PATH_MAX];
  const char *root = (conf->tools.root && conf->tools.root[0]) ? conf->tools.root : ".";
  if (!realpath(root, root_real)) {
    fprintf(stderr, "neo: tools.root realpath failed for %s\n", root);
    return -1;
  }

  NeoBuf body = {0};
  NeoBuf hist = {0};
  if (neo_buf_reserve(&body, NEO_BODY_INIT) != 0) return -1;
  if (neo_buf_reserve(&hist, 4096) != 0) {
    neo_buf_free(&body);
    return -1;
  }

  neo_buf_append(&body, "{\"model\":\"", 0);
  neo_json_escape(conf->model.name ? conf->model.name : "gpt", &body);
  neo_buf_append(&body, "\",\"messages\":[{\"role\":\"system\",\"content\":\"", 0);
  neo_json_escape(system_prompt, &body);
  neo_buf_append(&body, "\"}", 0);

  int pi;
  for (pi = 0; pi < n_prefix && prefix_messages && prefix_messages[pi].role && prefix_messages[pi].content; pi++) {
    const char *role = prefix_messages[pi].role;
    if (strcmp(role, "assistant") != 0) role = "user";
    neo_buf_append(&body, ",{\"role\":\"", 0);
    neo_buf_append(&body, role, 0);
    neo_buf_append(&body, "\",\"content\":\"", 0);
    neo_json_escape(prefix_messages[pi].content, &body);
    neo_buf_append(&body, "\"}", 0);
  }

  neo_buf_append(&body, ",{\"role\":\"user\",\"content\":\"", 0);
  neo_json_escape(user_message, &body);
  neo_buf_append(&body, "\"}", 0);

  int maxr = conf->tools.max_rounds > 0 ? conf->tools.max_rounds : 16;
  int round;

  for (round = 0; round < maxr; round++) {
    neo_buf_append(&body, hist.s ? hist.s : "", hist.len);
    neo_buf_append(&body, "],\"tools\":", 0);
    if (neo_build_tools_json(&body, conf) != 0) {
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }
    neo_buf_append(&body, ",\"tool_choice\":\"auto\",\"max_tokens\":", 0);
    char nbuf[32];
    snprintf(nbuf, sizeof(nbuf), "%d", conf->model.max_tokens > 0 ? conf->model.max_tokens : 4096);
    neo_buf_append(&body, nbuf, 0);
    neo_buf_append(&body, ",\"temperature\":", 0);
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%.2f", conf->model.temperature);
    neo_buf_append(&body, tbuf, 0);
    neo_buf_append(&body, "}", 0);

    llm_response_t raw = {0};
    if (llm_post_chat_completions_json(conf->model.base_url, conf->model.api_key, body.s, &raw) != 0) {
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }

    {
      yyjson_doc *doc;
      yyjson_val *root, *msg, *tc;
      int has_tools;

      doc = yyjson_read(raw.data, raw.size, 0);
      if (!doc) {
        fprintf(stderr, "neo: tool round JSON parse error\n");
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        return -1;
      }
      root = yyjson_doc_get_root(doc);
      msg = choice0_message(root);
      if (!msg) {
        fprintf(stderr, "neo: no message in response\n");
        yyjson_doc_free(doc);
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        return -1;
      }

      tc = yyjson_obj_get(msg, "tool_calls");
      has_tools = yyjson_is_arr(tc) && yyjson_arr_size(tc) > 0;

      if (!has_tools) {
        llm_response_free(out_text);
        out_text->data = NULL;
        out_text->size = 0;
        if (msg_content_to_llm(msg, out_text) != 0) {
          if (llm_extract_content_json(raw.data, out_text) != 0) {
            yyjson_doc_free(doc);
            llm_response_free(&raw);
            neo_buf_free(&body);
            neo_buf_free(&hist);
            return -1;
          }
        }
        yyjson_doc_free(doc);
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        return 0;
      }

      neo_buf_append(&hist, ",", 0);
      if (append_message_json(msg, &hist) != 0) {
        yyjson_doc_free(doc);
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        llm_response_free(out_text);
        out_text->data = NULL;
        out_text->size = 0;
        return -1;
      }

      {
        NeoToolCall calls[NEO_MAX_TOOLS_PER_TURN];
        int ncalls = 0;
        NeoBuf tres = {0};
        int ci;
        memset(calls, 0, sizeof(calls));
        parse_tool_calls_yy(msg, calls, &ncalls);
        yyjson_doc_free(doc);
        llm_response_free(&raw);

        if (ncalls == 0) {
          neo_buf_free(&body);
          neo_buf_free(&hist);
          return -1;
        }

        for (ci = 0; ci < ncalls; ci++) {
          if (run_one_tool(conf, root_real, &calls[ci], &tres) != 0) {
            neo_tool_calls_free(calls, ncalls);
            neo_buf_free(&tres);
            neo_buf_free(&body);
            neo_buf_free(&hist);
            return -1;
          }
          neo_buf_append(&hist, ",{\"role\":\"tool\",\"tool_call_id\":\"", 0);
          neo_json_escape(calls[ci].id, &hist);
          neo_buf_append(&hist, "\",\"content\":\"", 0);
          neo_json_escape(tres.s ? tres.s : "", &hist);
          neo_buf_append(&hist, "\"}", 0);
          tres.len = 0;
          if (tres.s) tres.s[0] = '\0';
        }
        neo_tool_calls_free(calls, ncalls);
        neo_buf_free(&tres);
      }
    }

    /* Rebuild body prefix for next POST */
    body.len = 0;
    if (body.s) body.s[0] = '\0';
    neo_buf_append(&body, "{\"model\":\"", 0);
    neo_json_escape(conf->model.name ? conf->model.name : "gpt", &body);
    neo_buf_append(&body, "\",\"messages\":[{\"role\":\"system\",\"content\":\"", 0);
    neo_json_escape(system_prompt, &body);
    neo_buf_append(&body, "\"}", 0);
    for (pi = 0; pi < n_prefix && prefix_messages && prefix_messages[pi].role && prefix_messages[pi].content; pi++) {
      const char *role = prefix_messages[pi].role;
      if (strcmp(role, "assistant") != 0) role = "user";
      neo_buf_append(&body, ",{\"role\":\"", 0);
      neo_buf_append(&body, role, 0);
      neo_buf_append(&body, "\",\"content\":\"", 0);
      neo_json_escape(prefix_messages[pi].content, &body);
      neo_buf_append(&body, "\"}", 0);
    }
    neo_buf_append(&body, ",{\"role\":\"user\",\"content\":\"", 0);
    neo_json_escape(user_message, &body);
    neo_buf_append(&body, "\"}", 0);
    /* hist carries assistant+tool messages for next iteration */
  }

  fprintf(stderr, "neo: tool max_rounds exceeded\n");
  neo_buf_free(&body);
  neo_buf_free(&hist);
  return -1;
#endif
}

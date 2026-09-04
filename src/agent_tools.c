/*
 * OpenAI-style tool_calls: read_file, write_file, list_dir; optional http_get (allowlist).
 */
#include "agent_tools.h"
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

#define JSMN_STATIC
#include "jsmn.h"

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

static const char NEO_TOOL_READ[] =
  "{\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"description\":\"Read a UTF-8 text file under the workspace root.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}}";

static const char NEO_TOOL_WRITE[] =
  "{\"type\":\"function\",\"function\":{\"name\":\"write_file\",\"description\":\"Create or overwrite a UTF-8 text file under the workspace root.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}}}";

static const char NEO_TOOL_LISTDIR[] =
  "{\"type\":\"function\",\"function\":{\"name\":\"list_dir\",\"description\":\"List names of files and subdirectories at a path under the workspace root (non-recursive).\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Relative directory; use . for workspace root\"}},\"required\":[\"path\"]}}}";

static const char NEO_TOOL_HTTPGET[] =
  "{\"type\":\"function\",\"function\":{\"name\":\"http_get\",\"description\":\"HTTPS GET for allowlisted hosts only (config tools.http_allow_hosts). No redirects. Body truncated.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}}}";

static int neo_build_tools_json(NeoBuf *b, const agent_config_t *conf) {
  if (neo_buf_append(b, "[", 0) != 0) return -1;
  if (neo_buf_append(b, NEO_TOOL_READ, strlen(NEO_TOOL_READ)) != 0) return -1;
  if (neo_buf_append(b, ",", 0) != 0) return -1;
  if (neo_buf_append(b, NEO_TOOL_WRITE, strlen(NEO_TOOL_WRITE)) != 0) return -1;
  if (neo_buf_append(b, ",", 0) != 0) return -1;
  if (neo_buf_append(b, NEO_TOOL_LISTDIR, strlen(NEO_TOOL_LISTDIR)) != 0) return -1;
  if (conf->tools.http_fetch_enabled && conf->tools.http_allow_hosts && conf->tools.http_allow_hosts[0]) {
    if (neo_buf_append(b, ",", 0) != 0) return -1;
    if (neo_buf_append(b, NEO_TOOL_HTTPGET, strlen(NEO_TOOL_HTTPGET)) != 0) return -1;
  }
  return neo_buf_append(b, "]", 0);
}

/*
 * Decode a JSON string token body as it appears on the wire (jsmn start..end
 * excludes quotes; sequences like \" and \\ are still escaped). Returns a
 * newly malloc'd C string; caller frees. Needed for tool "arguments" blobs.
 */
static char *neo_json_unescape_slice(const char *src, int len) {
  char *out;
  size_t j;
  int i;
  if (!src || len <= 0) {
    out = malloc(1);
    if (out) out[0] = '\0';
    return out;
  }
  out = malloc((size_t)len + 1);
  if (!out) return NULL;
  j = 0;
  i = 0;
  while (i < len) {
    unsigned char c = (unsigned char)src[i];
    if (c != '\\') {
      out[j++] = (char)c;
      i++;
      continue;
    }
    if (i + 1 >= len) break;
    i++;
    c = (unsigned char)src[i++];
    switch (c) {
    case '"': out[j++] = '"'; break;
    case '\\': out[j++] = '\\'; break;
    case '/': out[j++] = '/'; break;
    case 'b': out[j++] = '\b'; break;
    case 'f': out[j++] = '\f'; break;
    case 'n': out[j++] = '\n'; break;
    case 'r': out[j++] = '\r'; break;
    case 't': out[j++] = '\t'; break;
    case 'u': {
      int k;
      for (k = 0; k < 4 && i < len; k++) {
        unsigned char h = (unsigned char)src[i];
        if (!isxdigit((int)h)) break;
        i++;
      }
      out[j++] = '?';
      break;
    }
    default:
      out[j++] = (char)c;
      break;
    }
  }
  out[j] = '\0';
  return out;
}

static int neo_tok_skip(const jsmntok_t *t, int i) {
  int next = i + 1;
  int j;
  switch (t[i].type) {
  case JSMN_STRING:
  case JSMN_PRIMITIVE:
    return next;
  case JSMN_ARRAY:
    for (j = 0; j < t[i].size; j++)
      next = neo_tok_skip(t, next);
    return next;
  case JSMN_OBJECT:
    for (j = 0; j < t[i].size; j++) {
      next = neo_tok_skip(t, next);
      next = neo_tok_skip(t, next);
    }
    return next;
  default:
    return next;
  }
}

static int neo_tok_str_eq(const char *js, const jsmntok_t *t, const char *lit) {
  size_t L = strlen(lit);
  if (t->type != JSMN_STRING || (size_t)(t->end - t->start) != L) return 0;
  return strncmp(js + t->start, lit, L) == 0;
}

static int neo_tok_prim_eq(const char *js, const jsmntok_t *t, const char *lit) {
  size_t L = strlen(lit);
  if (t->type != JSMN_PRIMITIVE || (size_t)(t->end - t->start) != L) return 0;
  return strncmp(js + t->start, lit, L) == 0;
}

static int find_choice_message(const char *js, jsmntok_t *tok, int ntok, int *out_mi) {
  (void)ntok;
  if (tok[0].type != JSMN_OBJECT) return -1;
  int i = 1;
  for (int k = 0; k < tok[0].size; k++) {
    int val = i + 1;
    int next = neo_tok_skip(tok, val);
    if (neo_tok_str_eq(js, &tok[i], "choices")) {
      if (tok[val].type != JSMN_ARRAY || tok[val].size < 1) return -1;
      int ch0 = val + 1;
      if (tok[ch0].type != JSMN_OBJECT) return -1;
      int j = ch0 + 1;
      for (int m = 0; m < tok[ch0].size; m++) {
        int v2 = j + 1;
        int n2 = neo_tok_skip(tok, v2);
        if (neo_tok_str_eq(js, &tok[j], "message")) {
          *out_mi = v2;
          return 0;
        }
        j = n2;
      }
      return -1;
    }
    i = next;
  }
  return -1;
}

static int msg_find_tool_calls(const char *js, jsmntok_t *tok, int mi, int *out_tc) {
  if (tok[mi].type != JSMN_OBJECT) return -1;
  int cur = mi + 1;
  for (int j = 0; j < tok[mi].size; j++) {
    int val = cur + 1;
    int nxt = neo_tok_skip(tok, val);
    if (neo_tok_str_eq(js, &tok[cur], "tool_calls")) {
      if (tok[val].type == JSMN_ARRAY && tok[val].size > 0) {
        *out_tc = val;
        return 0;
      }
      return -1;
    }
    cur = nxt;
  }
  return -1;
}

static int msg_extract_content(const char *js, jsmntok_t *tok, int mi, llm_response_t *out) {
  out->data = NULL;
  out->size = 0;
  if (tok[mi].type != JSMN_OBJECT) return -1;
  int cur = mi + 1;
  for (int j = 0; j < tok[mi].size; j++) {
    int val = cur + 1;
    int nxt = neo_tok_skip(tok, val);
    if (neo_tok_str_eq(js, &tok[cur], "content")) {
      if (tok[val].type == JSMN_STRING) {
        int L = tok[val].end - tok[val].start;
        out->data = neo_json_unescape_slice(js + tok[val].start, L);
        if (!out->data) return -1;
        out->size = strlen(out->data);
        return 0;
      }
      if (tok[val].type == JSMN_PRIMITIVE && neo_tok_prim_eq(js, &tok[val], "null")) {
        out->data = malloc(1);
        if (!out->data) return -1;
        out->data[0] = '\0';
        out->size = 0;
        return 0;
      }
      return -1;
    }
    cur = nxt;
  }
  return -1;
}

static int msg_slice_raw(const char *js, jsmntok_t *tok, int mi, NeoBuf *dst) {
  int a = tok[mi].start;
  int b = tok[mi].end;
  if (a < 0 || b < a) return -1;
  return neo_buf_append(dst, js + a, (size_t)(b - a));
}

static void neo_tool_calls_free(NeoToolCall *tc, int n) {
  int i;
  for (i = 0; i < n; i++) {
    free(tc[i].id);
    free(tc[i].name);
    free(tc[i].arguments);
  }
}

static int parse_tool_calls(const char *js, jsmntok_t *tok, int tc_root, NeoToolCall *out, int *out_n) {
  *out_n = 0;
  if (tok[tc_root].type != JSMN_ARRAY) return -1;
  int el = tc_root + 1;
  for (int j = 0; j < tok[tc_root].size && *out_n < NEO_MAX_TOOLS_PER_TURN; j++) {
    if (tok[el].type != JSMN_OBJECT) return -1;
    NeoToolCall *t = &out[*out_n];
    t->id = t->name = t->arguments = NULL;
    int cur = el + 1;
    for (int k = 0; k < tok[el].size; k++) {
      int val = cur + 1;
      int nxt = neo_tok_skip(tok, val);
      if (neo_tok_str_eq(js, &tok[cur], "id") && tok[val].type == JSMN_STRING) {
        int L = tok[val].end - tok[val].start;
        t->id = neo_json_unescape_slice(js + tok[val].start, L);
      } else if (neo_tok_str_eq(js, &tok[cur], "function") && tok[val].type == JSMN_OBJECT) {
        int c2 = val + 1;
        for (int m = 0; m < tok[val].size; m++) {
          int v2 = c2 + 1;
          int n2 = neo_tok_skip(tok, v2);
          if (neo_tok_str_eq(js, &tok[c2], "name") && tok[v2].type == JSMN_STRING) {
            int L = tok[v2].end - tok[v2].start;
            t->name = neo_json_unescape_slice(js + tok[v2].start, L);
          } else if (neo_tok_str_eq(js, &tok[c2], "arguments") && tok[v2].type == JSMN_STRING) {
            int L = tok[v2].end - tok[v2].start;
            t->arguments = neo_json_unescape_slice(js + tok[v2].start, L);
          }
          c2 = n2;
        }
      }
      cur = nxt;
    }
    if (t->id && t->name && t->arguments) (*out_n)++;
    else {
      free(t->id);
      free(t->name);
      free(t->arguments);
      t->id = t->name = t->arguments = NULL;
    }
    el = neo_tok_skip(tok, el);
  }
  return 0;
}

static int jsmn_extract_string_field(const char *obj, const char *key, char *out, size_t out_cap) {
  jsmn_parser p;
  jsmntok_t tok[128];
  jsmn_init(&p);
  size_t olen = strlen(obj);
  size_t keylen = strlen(key);
  int r = jsmn_parse(&p, obj, olen, tok, (unsigned int)(sizeof(tok) / sizeof(tok[0])));
  if (r < 1 || tok[0].type != JSMN_OBJECT) return -1;
  int cur = 1;
  for (int j = 0; j < tok[0].size; j++) {
    int val = cur + 1;
    int nxt = neo_tok_skip(tok, val);
    if (tok[cur].type == JSMN_STRING && (size_t)(tok[cur].end - tok[cur].start) == keylen &&
        strncmp(obj + tok[cur].start, key, keylen) == 0) {
      if (tok[val].type != JSMN_STRING) return -1;
      int L = tok[val].end - tok[val].start;
      if (L < 0) return -1;
      {
        char *tmp = neo_json_unescape_slice(obj + tok[val].start, L);
        size_t ulen;
        if (!tmp) return -1;
        ulen = strlen(tmp);
        if (ulen >= out_cap) {
          free(tmp);
          return -1;
        }
        memcpy(out, tmp, ulen + 1);
        free(tmp);
      }
      return 0;
    }
    cur = nxt;
  }
  return -1;
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
  if (jsmn_extract_string_field(args_json, "url", urlbuf, sizeof(urlbuf)) != 0)
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
  if (jsmn_extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
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
  if (jsmn_extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
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
  if (jsmn_extract_string_field(args_json, "path", rel, sizeof(rel)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing path", 0);
  }
  if (jsmn_extract_string_field(args_json, "content", content, ccap) != 0) {
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
  return neo_buf_append(result, "ERROR: unknown tool", 0);
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

    jsmn_parser pr;
    jsmntok_t tok[16384];
    jsmn_init(&pr);
    int tr = jsmn_parse(&pr, raw.data, raw.size, tok, (unsigned int)(sizeof(tok) / sizeof(tok[0])));
    if (tr < 0) {
      fprintf(stderr, "neo: tool round JSON parse error %d\n", tr);
      llm_response_free(&raw);
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }

    int mi = -1;
    if (find_choice_message(raw.data, tok, tr, &mi) != 0) {
      fprintf(stderr, "neo: no message in response\n");
      llm_response_free(&raw);
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }

    int tc_idx = -1;
    int has_tools = (msg_find_tool_calls(raw.data, tok, mi, &tc_idx) == 0);

    if (!has_tools) {
      llm_response_free(out_text);
      out_text->data = NULL;
      out_text->size = 0;
      if (msg_extract_content(raw.data, tok, mi, out_text) != 0) {
        /* Fallback: whole-response content extractor */
        if (llm_extract_content_json(raw.data, out_text) != 0) {
          llm_response_free(&raw);
          neo_buf_free(&body);
          neo_buf_free(&hist);
          return -1;
        }
      }
      llm_response_free(&raw);
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return 0;
    }

    neo_buf_append(&hist, ",", 0);
    if (msg_slice_raw(raw.data, tok, mi, &hist) != 0) {
      llm_response_free(&raw);
      neo_buf_free(&body);
      neo_buf_free(&hist);
      llm_response_free(out_text);
      out_text->data = NULL;
      out_text->size = 0;
      return -1;
    }

    NeoToolCall calls[NEO_MAX_TOOLS_PER_TURN];
    int ncalls = 0;
    memset(calls, 0, sizeof(calls));
    parse_tool_calls(raw.data, tok, tc_idx, calls, &ncalls);
    llm_response_free(&raw);

    if (ncalls == 0) {
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }

    NeoBuf tres = {0};
    int ci;
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

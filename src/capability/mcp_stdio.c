/*
 * MCP stdio：子进程 JSON-RPC（initialize → tools/list → tools/call）。
 * 结果写入能力矩阵；单 server 失败不影响其它行。
 */
#include "mcp_stdio.h"
#include "yyjson.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define MCP_MAX_SERVERS 16
#define MCP_LINE_MAX (1024 * 1024)
#define MCP_TIMEOUT_MS 15000

typedef struct {
  char *name;
  pid_t pid;
  int in_fd;  /* write to child stdin */
  int out_fd; /* read from child stdout */
  int next_id;
} mcp_proc_t;

static mcp_proc_t g_procs[MCP_MAX_SERVERS];
static int g_nprocs;

static void sanitize_id(char *s) {
  for (; s && *s; s++) {
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
          *s == '_'))
      *s = '_';
  }
}

static mcp_proc_t *find_proc(const char *name) {
  int i;
  for (i = 0; i < g_nprocs; i++) {
    if (g_procs[i].name && strcmp(g_procs[i].name, name) == 0) return &g_procs[i];
  }
  return NULL;
}

void mcp_stdio_shutdown_all(void) {
#if defined(__APPLE__) || defined(__linux__)
  int i;
  for (i = 0; i < g_nprocs; i++) {
    if (g_procs[i].in_fd >= 0) close(g_procs[i].in_fd);
    if (g_procs[i].out_fd >= 0) close(g_procs[i].out_fd);
    if (g_procs[i].pid > 0) {
      kill(g_procs[i].pid, SIGTERM);
      waitpid(g_procs[i].pid, NULL, 0);
    }
    free(g_procs[i].name);
    memset(&g_procs[i], 0, sizeof(g_procs[i]));
  }
  g_nprocs = 0;
#else
  g_nprocs = 0;
#endif
}

#if defined(__APPLE__) || defined(__linux__)

static int read_line_timeout(int fd, char *buf, size_t cap, int timeout_ms) {
  size_t n = 0;
  while (n + 1 < cap) {
    struct pollfd pfd;
    int pr;
    char c;
    ssize_t r;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return -1;
    r = read(fd, &c, 1);
    if (r <= 0) return -1;
    if (c == '\n') {
      buf[n] = '\0';
      return 0;
    }
    if (c != '\r') buf[n++] = c;
  }
  return -1;
}

static int write_line(int fd, const char *line) {
  size_t n = strlen(line);
  if (write(fd, line, n) != (ssize_t)n) return -1;
  if (write(fd, "\n", 1) != 1) return -1;
  return 0;
}

static int rpc_roundtrip(mcp_proc_t *p, const char *req_json, char **out_line) {
  char *buf;
  *out_line = NULL;
  if (write_line(p->in_fd, req_json) != 0) return -1;
  buf = malloc(MCP_LINE_MAX);
  if (!buf) return -1;
  if (read_line_timeout(p->out_fd, buf, MCP_LINE_MAX, MCP_TIMEOUT_MS) != 0) {
    free(buf);
    return -1;
  }
  *out_line = buf;
  return 0;
}

static mcp_proc_t *spawn_server(const mcp_server_config_t *srv) {
  int in_pipe[2], out_pipe[2];
  pid_t pid;
  mcp_proc_t *p;
  char **argv;
  int i, argc;
  if (g_nprocs >= MCP_MAX_SERVERS) return NULL;
  if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) return NULL;
  pid = fork();
  if (pid < 0) {
    close(in_pipe[0]);
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(out_pipe[1]);
    return NULL;
  }
  if (pid == 0) {
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);
    close(in_pipe[0]);
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(out_pipe[1]);
    /* leave stderr as-is for server logs */
    argc = 1 + srv->args_count;
    argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!argv) _exit(127);
    argv[0] = srv->command;
    for (i = 0; i < srv->args_count; i++) argv[i + 1] = srv->args[i];
    execvp(srv->command, argv);
    _exit(127);
  }
  close(in_pipe[0]);
  close(out_pipe[1]);
  p = &g_procs[g_nprocs++];
  memset(p, 0, sizeof(*p));
  p->name = strdup(srv->name);
  p->pid = pid;
  p->in_fd = in_pipe[1];
  p->out_fd = out_pipe[0];
  p->next_id = 1;
  return p;
}

static int ensure_initialized(mcp_proc_t *p) {
  char req[512];
  char *resp = NULL;
  yyjson_doc *doc;
  yyjson_val *root, *err;
  int id = p->next_id++;
  snprintf(req, sizeof(req),
           "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
           "\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},"
           "\"clientInfo\":{\"name\":\"neoclaw\",\"version\":\"0.1\"}}}",
           id);
  if (rpc_roundtrip(p, req, &resp) != 0) return -1;
  doc = yyjson_read(resp, strlen(resp), 0);
  free(resp);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  err = yyjson_obj_get(root, "error");
  if (err) {
    yyjson_doc_free(doc);
    return -1;
  }
  yyjson_doc_free(doc);
  /* notification — no response */
  write_line(p->in_fd, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}");
  return 0;
}

static int load_one_server(const mcp_server_config_t *srv, capability_matrix_t *m) {
  /* fork+管道拉起一个 stdio MCP，list 工具后 capability_matrix_add_mcp。 */
  mcp_proc_t *p;
  char req[128];
  char *resp = NULL;
  yyjson_doc *doc;
  yyjson_val *root, *result, *tools, *tool;
  size_t i, n;
  int id;

  if (!srv->enabled) return 0;
  if (srv->url && srv->url[0]) {
    fprintf(stderr, "neo: mcp server '%s': url transport not implemented in v1; skipping\n",
            srv->name);
    return 0;
  }
  if (find_proc(srv->name)) {
    /* already up — still ensure tools listed into this matrix build */
  }
  p = find_proc(srv->name);
  if (!p) {
    p = spawn_server(srv);
    if (!p) {
      fprintf(stderr, "neo: mcp server '%s': spawn failed\n", srv->name);
      return 0;
    }
    if (ensure_initialized(p) != 0) {
      fprintf(stderr, "neo: mcp server '%s': initialize failed\n", srv->name);
      return 0;
    }
  }

  id = p->next_id++;
  snprintf(req, sizeof(req), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/list\",\"params\":{}}",
           id);
  if (rpc_roundtrip(p, req, &resp) != 0) {
    fprintf(stderr, "neo: mcp server '%s': tools/list failed\n", srv->name);
    return 0;
  }
  doc = yyjson_read(resp, strlen(resp), 0);
  free(resp);
  if (!doc) return 0;
  root = yyjson_doc_get_root(doc);
  result = yyjson_obj_get(root, "result");
  tools = result ? yyjson_obj_get(result, "tools") : NULL;
  if (!yyjson_is_arr(tools)) {
    yyjson_doc_free(doc);
    return 0;
  }
  n = yyjson_arr_size(tools);
  for (i = 0; i < n; i++) {
    const char *tname, *tdesc;
    char *params = NULL;
    char matrix_name[256];
    char san_server[128];
    char san_tool[128];
    yyjson_val *schema;
    tool = yyjson_arr_get(tools, i);
    if (!yyjson_is_obj(tool)) continue;
    tname = yyjson_get_str(yyjson_obj_get(tool, "name"));
    tdesc = yyjson_get_str(yyjson_obj_get(tool, "description"));
    if (!tname) continue;
    schema = yyjson_obj_get(tool, "inputSchema");
    if (!schema) schema = yyjson_obj_get(tool, "input_schema");
    if (yyjson_is_obj(schema)) params = yyjson_val_write(schema, 0, NULL);
    snprintf(san_server, sizeof(san_server), "%s", srv->name);
    snprintf(san_tool, sizeof(san_tool), "%s", tname);
    sanitize_id(san_server);
    sanitize_id(san_tool);
    snprintf(matrix_name, sizeof(matrix_name), "mcp_%s_%s", san_server, san_tool);
    capability_matrix_add_mcp(m, matrix_name, tdesc ? tdesc : tname,
                              params ? params : "{\"type\":\"object\"}", srv->name, tname);
    free(params);
  }
  yyjson_doc_free(doc);
  return 0;
}

int mcp_stdio_load_into_matrix(const agent_config_t *conf, capability_matrix_t *m) {
  int i;
  static int atexit_set;
  if (!conf || !m) return 0;
  if (!atexit_set) {
    atexit(mcp_stdio_shutdown_all);
    atexit_set = 1;
  }
  for (i = 0; i < conf->tools.mcp_server_count; i++)
    load_one_server(&conf->tools.mcp_servers[i], m);
  return 0;
}

int mcp_stdio_call(const agent_config_t *conf, const cap_row_t *row, const char *args_json,
                   char **out, size_t *out_len) {
  mcp_proc_t *p;
  char *req = NULL;
  char *resp = NULL;
  yyjson_doc *doc;
  yyjson_val *root, *result, *content, *el, *textv, *err;
  size_t i, n;
  int id;
  size_t need;
  const char *args = args_json ? args_json : "{}";

  if (out) *out = NULL;
  if (out_len) *out_len = 0;
  if (!conf || !row || !row->mcp_server || !row->mcp_tool || !out) return -1;

  p = find_proc(row->mcp_server);
  if (!p) {
    int si;
    for (si = 0; si < conf->tools.mcp_server_count; si++) {
      const mcp_server_config_t *srv = &conf->tools.mcp_servers[si];
      if (srv->name && strcmp(srv->name, row->mcp_server) == 0) {
        p = spawn_server(srv);
        if (p && ensure_initialized(p) != 0) p = NULL;
        break;
      }
    }
  }
  if (!p) {
    *out = strdup("ERROR: mcp server not running");
    if (out_len) *out_len = strlen(*out);
    return 0;
  }

  id = p->next_id++;
  need = strlen(args) + strlen(row->mcp_tool) + 128;
  req = malloc(need);
  if (!req) return -1;
  snprintf(req, need,
           "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\","
           "\"arguments\":%s}}",
           id, row->mcp_tool, args);
  if (rpc_roundtrip(p, req, &resp) != 0) {
    free(req);
    *out = strdup("ERROR: mcp tools/call failed");
    if (out_len) *out_len = strlen(*out);
    return 0;
  }
  free(req);
  doc = yyjson_read(resp, strlen(resp), 0);
  free(resp);
  if (!doc) {
    *out = strdup("ERROR: mcp bad JSON");
    if (out_len) *out_len = strlen(*out);
    return 0;
  }
  root = yyjson_doc_get_root(doc);
  err = yyjson_obj_get(root, "error");
  if (err) {
    char *es = yyjson_val_write(err, 0, NULL);
    *out = es ? es : strdup("ERROR: mcp error");
    if (out_len) *out_len = strlen(*out);
    yyjson_doc_free(doc);
    return 0;
  }
  result = yyjson_obj_get(root, "result");
  content = result ? yyjson_obj_get(result, "content") : NULL;
  if (yyjson_is_arr(content)) {
    size_t total = 0;
    char *acc;
    n = yyjson_arr_size(content);
    for (i = 0; i < n; i++) {
      el = yyjson_arr_get(content, i);
      textv = yyjson_obj_get(el, "text");
      if (yyjson_is_str(textv)) total += yyjson_get_len(textv) + 1;
    }
    acc = malloc(total + 1);
    if (!acc) {
      yyjson_doc_free(doc);
      return -1;
    }
    acc[0] = '\0';
    for (i = 0; i < n; i++) {
      const char *t;
      el = yyjson_arr_get(content, i);
      textv = yyjson_obj_get(el, "text");
      if (!yyjson_is_str(textv)) continue;
      t = yyjson_get_str(textv);
      if (acc[0]) strcat(acc, "\n");
      strcat(acc, t ? t : "");
    }
    *out = acc;
    if (out_len) *out_len = strlen(acc);
  } else {
    *out = strdup("(empty mcp result)");
    if (out_len) *out_len = strlen(*out);
  }
  yyjson_doc_free(doc);
  return 0;
}

#else /* !apple/linux */

int mcp_stdio_load_into_matrix(const agent_config_t *conf, capability_matrix_t *m) {
  (void)conf;
  (void)m;
  return 0;
}

int mcp_stdio_call(const agent_config_t *conf, const cap_row_t *row, const char *args_json,
                   char **out, size_t *out_len) {
  (void)conf;
  (void)row;
  (void)args_json;
  if (out) *out = strdup("ERROR: mcp unsupported on this platform");
  if (out_len && out && *out) *out_len = strlen(*out);
  return 0;
}

#endif

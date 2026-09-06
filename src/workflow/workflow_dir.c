/*
 * DAG 目录实现：与 capability_dir 对称，供 catalog 选型与 workflow run。
 */
#include "workflow_dir.h"
#include "yyjson.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *dup_s(const char *s) {
  size_t n;
  char *o;
  if (!s) return NULL;
  n = strlen(s);
  o = malloc(n + 1);
  if (!o) return NULL;
  memcpy(o, s, n + 1);
  return o;
}

static int path_safe_rel(const char *rel) {
  if (!rel || !rel[0] || rel[0] == '/') return 0;
  if (strstr(rel, "..")) return 0;
  return 1;
}

static int ends_with(const char *s, const char *suf) {
  size_t ns, nf;
  if (!s || !suf) return 0;
  ns = strlen(s);
  nf = strlen(suf);
  return ns >= nf && strcmp(s + ns - nf, suf) == 0;
}

static int read_manifest(const char *dir_abs, char ***load_dirs, int *load_n) {
  char path[PATH_MAX];
  yyjson_doc *doc;
  yyjson_val *root, *load;
  yyjson_read_err err;
  size_t i, n;

  *load_dirs = NULL;
  *load_n = 0;
  if (snprintf(path, sizeof(path), "%s/manifest.json5", dir_abs) >= (int)sizeof(path)) return -1;
  if (access(path, R_OK) != 0) {
    char **a = calloc(1, sizeof(char *));
    if (!a) return -1;
    a[0] = dup_s("library");
    if (!a[0]) {
      free(a);
      return -1;
    }
    *load_dirs = a;
    *load_n = 1;
    return 0;
  }
  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: workflow dir: bad manifest %s\n", path);
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  load = yyjson_obj_get(root, "load");
  if (!load) {
    char **a = calloc(1, sizeof(char *));
    if (!a) {
      yyjson_doc_free(doc);
      return -1;
    }
    a[0] = dup_s("library");
    *load_dirs = a;
    *load_n = 1;
    yyjson_doc_free(doc);
    return 0;
  }
  if (!yyjson_is_arr(load)) {
    fprintf(stderr, "neo: workflow dir: manifest.load must be array\n");
    yyjson_doc_free(doc);
    return -1;
  }
  n = yyjson_arr_size(load);
  {
    char **a = calloc(n ? n : 1, sizeof(char *));
    if (!a) {
      yyjson_doc_free(doc);
      return -1;
    }
    for (i = 0; i < n; i++) {
      yyjson_val *el = yyjson_arr_get(load, i);
      if (!yyjson_is_str(el) || !path_safe_rel(yyjson_get_str(el))) {
        size_t j;
        for (j = 0; j < i; j++) free(a[j]);
        free(a);
        yyjson_doc_free(doc);
        return -1;
      }
      a[i] = dup_s(yyjson_get_str(el));
      if (!a[i]) {
        size_t j;
        for (j = 0; j < i; j++) free(a[j]);
        free(a);
        yyjson_doc_free(doc);
        return -1;
      }
    }
    *load_dirs = a;
    *load_n = (int)n;
  }
  yyjson_doc_free(doc);
  return 0;
}

static int load_one_file(agent_config_t *conf, const char *path) {
  yyjson_doc *doc;
  yyjson_val *root;
  yyjson_read_err err;
  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: workflow dir: cannot parse %s (%s)\n", path, err.msg ? err.msg : "?");
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (config_append_workflow_val(conf, root, path) != 0) {
    yyjson_doc_free(doc);
    return -1;
  }
  yyjson_doc_free(doc);
  return 0;
}

static int load_subdir(agent_config_t *conf, const char *dir_abs) {
  DIR *d;
  struct dirent *de;
  d = opendir(dir_abs);
  if (!d) {
    fprintf(stderr, "neo: workflow dir: cannot open %s: %s\n", dir_abs, strerror(errno));
    return -1;
  }
  while ((de = readdir(d)) != NULL) {
    char path[PATH_MAX];
    struct stat st;
    if (de->d_name[0] == '.') continue;
    if (!ends_with(de->d_name, ".json5") && !ends_with(de->d_name, ".json")) continue;
    if (snprintf(path, sizeof(path), "%s/%s", dir_abs, de->d_name) >= (int)sizeof(path)) {
      closedir(d);
      return -1;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if (load_one_file(conf, path) != 0) {
      closedir(d);
      return -1;
    }
  }
  closedir(d);
  return 0;
}

int workflow_dir_load_into_config(agent_config_t *conf) {
  char dir_abs[PATH_MAX];
  char **load_dirs = NULL;
  int load_n = 0, i;
  const char *dir;

  if (!conf || !conf->workflow_directory || !conf->workflow_directory[0]) return 0;
  dir = conf->workflow_directory;
  if (dir[0] == '/' || strstr(dir, "..")) {
    fprintf(stderr, "neo: workflow_directory must be a relative path without ..\n");
    return -1;
  }
  if (!realpath(dir, dir_abs)) {
    fprintf(stderr, "neo: workflow_directory realpath failed for %s: %s\n", dir, strerror(errno));
    return -1;
  }
  if (read_manifest(dir_abs, &load_dirs, &load_n) != 0) return -1;
  for (i = 0; i < load_n; i++) {
    char sub[PATH_MAX];
    if (!load_dirs[i] || !path_safe_rel(load_dirs[i])) {
      for (i = 0; i < load_n; i++) free(load_dirs[i]);
      free(load_dirs);
      return -1;
    }
    if (snprintf(sub, sizeof(sub), "%s/%s", dir_abs, load_dirs[i]) >= (int)sizeof(sub)) {
      for (i = 0; i < load_n; i++) free(load_dirs[i]);
      free(load_dirs);
      return -1;
    }
    if (load_subdir(conf, sub) != 0) {
      for (i = 0; i < load_n; i++) free(load_dirs[i]);
      free(load_dirs);
      return -1;
    }
  }
  for (i = 0; i < load_n; i++) free(load_dirs[i]);
  free(load_dirs);
  return 0;
}

static int catalog_append(char **buf, size_t *len, size_t *cap, const char *line) {
  size_t need = strlen(line);
  if (*len + need + 1 > *cap) {
    size_t ncap = *cap ? *cap * 2 : 512;
    char *nb;
    while (ncap < *len + need + 1) ncap *= 2;
    nb = realloc(*buf, ncap);
    if (!nb) return -1;
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, line, need + 1);
  *len += need;
  return 0;
}

char *workflow_dir_catalog_listing(const agent_config_t *conf) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  int i;
  if (!conf || conf->workflow_count < 1) return dup_s("(no DAG catalog — invent a workflows array)\n");
  for (i = 0; i < conf->workflow_count; i++) {
    const workflow_t *wf = &conf->workflows[i];
    char line[1536];
    if (!wf->name) continue;
    /* 前缀 DAG: 与能力矩阵名单区分，降低 planner 把 tool 名写入 use 的概率。 */
    snprintf(line, sizeof(line), "- DAG: %s — %s\n", wf->name,
             wf->description ? wf->description : "(no description)");
    if (catalog_append(&buf, &len, &cap, line) != 0) {
      free(buf);
      return NULL;
    }
    if (wf->when) {
      snprintf(line, sizeof(line), "  when: %s\n", wf->when);
      if (catalog_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
    if (wf->when_not) {
      snprintf(line, sizeof(line), "  when_not: %s\n", wf->when_not);
      if (catalog_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
    if (wf->requires) {
      snprintf(line, sizeof(line), "  requires: %s\n", wf->requires);
      if (catalog_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
    if (wf->outcome) {
      snprintf(line, sizeof(line), "  outcome: %s\n", wf->outcome);
      if (catalog_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
    if (wf->tags) {
      snprintf(line, sizeof(line), "  tags: %s\n", wf->tags);
      if (catalog_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
  }
  if (!buf) return dup_s("(no DAG catalog — invent a workflows array)\n");
  return buf;
}

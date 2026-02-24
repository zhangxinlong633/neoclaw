#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAX_STR 512
#define MAX_PATHS 32

static char *dup_str(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

static char *trim_quotes(char *s) {
  char *p = s;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '"') { p++; char *e = strchr(p, '"'); if (e) *e = '\0'; }
  else { char *e = p + strcspn(p, " \t\r\n"); *e = '\0'; }
  return p;
}

static void free_path_list(char **paths, int n) {
  int i;
  if (!paths) return;
  for (i = 0; i < n; i++) free(paths[i]);
  free(paths);
}

void config_init(agent_config_t *c) {
  memset(c, 0, sizeof(*c));
}

void config_free(agent_config_t *c) {
  free(c->model.provider);
  free(c->model.base_url);
  free(c->model.name);
  free(c->model.api_key);
  c->model.provider = c->model.base_url = c->model.name = c->model.api_key = NULL;
  free_path_list(c->bootstrap.paths, c->bootstrap.path_count);
  c->bootstrap.paths = NULL;
  c->bootstrap.path_count = 0;
  free_path_list(c->skills.paths, c->skills.path_count);
  free(c->skills.priority);
  free(c->skills.directory);
  free_path_list(c->skills.high_priority, c->skills.high_priority_count);
  c->skills.paths = NULL;
  c->skills.priority = NULL;
  c->skills.directory = NULL;
  c->skills.high_priority = NULL;
  c->skills.path_count = 0;
  c->skills.high_priority_count = 0;
  free(c->memory.path);
  c->memory.path = NULL;
}

static void add_path(char ***paths, int *count, const char *val, int max_count) {
  if (*count >= max_count) return;
  char *dup = dup_str(val);
  if (!dup) return;
  char **np = realloc(*paths, (*count + 1) * sizeof(char *));
  if (!np) { free(dup); return; }
  *paths = np;
  (*paths)[*count] = dup;
  (*count)++;
}

int config_load_file(agent_config_t *c, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;

  char line[1024];
  int in_model = 0, in_skills = 0, in_memory = 0, in_bootstrap = 0, in_session = 0, in_high_priority = 0;
  c->memory.max_chars = 4000;
  c->model.max_tokens = 4096;
  c->model.temperature = 0.7;
  c->bootstrap.max_chars_per_file = 8000;
  c->session_max_turns = 10;

  while (fgets(line, sizeof(line), f)) {
    char *t = line;
    while (*t == ' ' || *t == '\t') t++;
    if (*t == '#' || *t == '\n' || *t == '\0') continue;

    if (strncmp(t, "model:", 6) == 0) { in_model = 1; in_skills = 0; in_memory = 0; in_bootstrap = 0; in_session = 0; continue; }
    if (strncmp(t, "skills:", 7) == 0) { in_skills = 1; in_high_priority = 0; in_model = 0; in_memory = 0; in_bootstrap = 0; in_session = 0; continue; }
    if (strncmp(t, "memory:", 7) == 0) { in_memory = 1; in_model = 0; in_skills = 0; in_bootstrap = 0; in_session = 0; continue; }
    if (strncmp(t, "bootstrap:", 10) == 0) { in_bootstrap = 1; in_model = 0; in_skills = 0; in_memory = 0; in_session = 0; continue; }
    if (strncmp(t, "session:", 8) == 0) { in_session = 1; in_model = 0; in_skills = 0; in_memory = 0; in_bootstrap = 0; continue; }

    if (in_model) {
      if (strncmp(t, "base_url:", 9) == 0) {
        free(c->model.base_url);
        c->model.base_url = dup_str(trim_quotes(t + 9));
      } else if (strncmp(t, "name:", 5) == 0) {
        free(c->model.name);
        c->model.name = dup_str(trim_quotes(t + 5));
      } else if (strncmp(t, "provider:", 9) == 0) {
        free(c->model.provider);
        c->model.provider = dup_str(trim_quotes(t + 9));
      } else if (strncmp(t, "api_key:", 8) == 0) {
        free(c->model.api_key);
        c->model.api_key = dup_str(trim_quotes(t + 8));
      } else if (strncmp(t, "max_tokens:", 11) == 0)
        c->model.max_tokens = atoi(t + 11);
      else if (strncmp(t, "temperature:", 12) == 0)
        c->model.temperature = atof(t + 12);
    }
    if (in_memory) {
      if (strncmp(t, "path:", 5) == 0) {
        free(c->memory.path);
        c->memory.path = dup_str(trim_quotes(t + 5));
      } else if (strncmp(t, "max_chars:", 10) == 0)
        c->memory.max_chars = atoi(t + 10);
    }
    if (in_bootstrap) {
      if (strncmp(t, "- path:", 7) == 0)
        add_path(&c->bootstrap.paths, &c->bootstrap.path_count, trim_quotes(t + 7), MAX_PATHS);
      else if (strncmp(t, "max_chars_per_file:", 19) == 0)
        c->bootstrap.max_chars_per_file = atoi(t + 19);
    }
    if (in_skills && strncmp(t, "- path:", 7) == 0) {
      add_path(&c->skills.paths, &c->skills.path_count, trim_quotes(t + 7), MAX_PATHS);
      if (c->skills.path_count > 0) {
        int *np = realloc(c->skills.priority, c->skills.path_count * sizeof(int));
        if (np) { c->skills.priority = np; c->skills.priority[c->skills.path_count - 1] = 0; }
      }
    }
    if (in_skills && (strstr(t, "priority: high") != NULL || strstr(t, "priority: 1") != NULL))
      if (c->skills.path_count > 0 && c->skills.priority)
        c->skills.priority[c->skills.path_count - 1] = 1;
    if (in_skills && strncmp(t, "unmatched:", 10) == 0) {
      in_high_priority = 0;
      t += 10;
      while (*t == ' ' || *t == '\t') t++;
      if (*t == '#') { /* no value */ } else if (strncmp(t, "skip", 4) == 0 && (t[4] == ' ' || t[4] == '\t' || t[4] == '#' || t[4] == '\0'))
        c->skills.unmatched = 1;
      else if (strncmp(t, "index", 5) == 0 && (t[5] == ' ' || t[5] == '\t' || t[5] == '#' || t[5] == '\0'))
        c->skills.unmatched = 0;
    }
    if (in_skills && strncmp(t, "directory:", 10) == 0) {
      in_high_priority = 0;
      free(c->skills.directory);
      c->skills.directory = dup_str(trim_quotes(t + 10));
    }
    if (in_skills && strncmp(t, "high_priority:", 14) == 0) { in_high_priority = 1; continue; }
    if (in_skills && in_high_priority && strncmp(t, "- ", 2) == 0) {
      if (c->skills.high_priority_count < MAX_PATHS)
        add_path(&c->skills.high_priority, &c->skills.high_priority_count, trim_quotes(t + 2), MAX_PATHS);
      continue;
    }
    if (in_skills && (strncmp(t, "directory:", 10) == 0 || strncmp(t, "unmatched:", 10) == 0 || strncmp(t, "- path:", 7) == 0))
      in_high_priority = 0;
    if (in_session && strncmp(t, "max_turns:", 10) == 0)
      c->session_max_turns = atoi(t + 10);
  }
  fclose(f);
  if (c->session_max_turns <= 0) c->session_max_turns = 10;

#if defined(__linux__) || defined(__APPLE__)
  if (c->skills.directory && c->skills.directory[0]) {
    DIR *dir = opendir(c->skills.directory);
    if (dir) {
      char **scanned = NULL;
      int n_scan = 0;
      struct dirent *e;
      while (n_scan < MAX_PATHS && (e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char subpath[1024];
        snprintf(subpath, sizeof(subpath), "%s/%s/SKILL.md", c->skills.directory, e->d_name);
        struct stat st;
        if (stat(subpath, &st) == 0 && S_ISREG(st.st_mode)) {
          char *dup = dup_str(subpath);
          if (dup) {
            char **np = realloc(scanned, (n_scan + 1) * sizeof(char *));
            if (np) { scanned = np; scanned[n_scan++] = dup; }
            else free(dup);
          }
        }
      }
      closedir(dir);
      if (n_scan > 0) {
        char **new_paths = malloc((n_scan + c->skills.path_count) * sizeof(char *));
        int *new_pri = malloc((n_scan + c->skills.path_count) * sizeof(int));
        if (new_paths && new_pri) {
          for (int i = 0; i < n_scan; i++) { new_paths[i] = scanned[i]; new_pri[i] = 0; }
          for (int i = 0; i < c->skills.path_count; i++) {
            new_paths[n_scan + i] = c->skills.paths[i];
            new_pri[n_scan + i] = c->skills.priority[i];
          }
          free(c->skills.paths);
          free(c->skills.priority);
          c->skills.paths = new_paths;
          c->skills.priority = new_pri;
          c->skills.path_count = n_scan + c->skills.path_count;
        } else { free(new_paths); free(new_pri); free_path_list(scanned, n_scan); }
      } else
        free_path_list(scanned, n_scan);
    }
  }
  /* Apply high_priority: set priority[i]=1 if path or its dirname (e.g. nanjing) is in list */
  if (c->skills.high_priority_count > 0 && c->skills.priority) {
    int i, j;
    for (i = 0; i < c->skills.path_count; i++) {
      const char *path = c->skills.paths[i];
      const char *last_slash = path ? strrchr(path, '/') : NULL;
      const char *name = path;
      size_t namelen = 0;
      if (last_slash && last_slash > path) {
        const char *prev = last_slash;
        while (prev > path && prev[-1] != '/') prev--;
        if (prev > path) { name = prev + 1; namelen = (size_t)(last_slash - name); }
      }
      for (j = 0; j < c->skills.high_priority_count; j++) {
        const char *hp = c->skills.high_priority[j];
        if (!hp || !*hp) continue;
        if (strcmp(path, hp) == 0) { c->skills.priority[i] = 1; break; }
        if (namelen > 0 && strlen(hp) == namelen && strncmp(name, hp, namelen) == 0) { c->skills.priority[i] = 1; break; }
        if (strstr(path, hp) != NULL) { c->skills.priority[i] = 1; break; }
      }
    }
  }
#endif

  if (!c->model.base_url) c->model.base_url = dup_str("http://127.0.0.1:11434/v1");
  if (!c->model.name) c->model.name = dup_str("qwen3:8b");
  if (!c->model.provider) c->model.provider = dup_str("ollama");
  if (c->model.max_tokens <= 0) c->model.max_tokens = 4096;
  return 0;
}

void config_apply_env(agent_config_t *c) {
  const char *v;
  v = getenv("NEO_MODEL");
  if (v && v[0]) { free(c->model.name); c->model.name = dup_str(v); }
  v = getenv("NEO_API_KEY");
  if (v && v[0]) { free(c->model.api_key); c->model.api_key = dup_str(v); }
  v = getenv("NEO_CONFIG");
  (void)v;
}

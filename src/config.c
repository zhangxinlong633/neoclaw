#include "config.h"
#include "yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAX_STR 512
#define MAX_PATHS 32
#define MAX_COMMANDS 32
#define MAX_ARGV 16

static char *dup_str(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

static void free_path_list(char **paths, int n) {
  int i;
  if (!paths) return;
  for (i = 0; i < n; i++) free(paths[i]);
  free(paths);
}

static void free_tool_commands(tool_command_t *cmds, int n) {
  int i, j;
  if (!cmds) return;
  for (i = 0; i < n; i++) {
    free(cmds[i].name);
    free(cmds[i].description);
    if (cmds[i].argv) {
      for (j = 0; j < cmds[i].argv_count; j++) free(cmds[i].argv[j]);
      free(cmds[i].argv);
    }
  }
  free(cmds);
}

static int tool_command_name_reserved(const char *name) {
  return name && (strcmp(name, "read_file") == 0 || strcmp(name, "write_file") == 0 ||
                  strcmp(name, "list_dir") == 0 || strcmp(name, "http_get") == 0);
}

static void free_workflows(workflow_t *wfs, int n) {
  int i, j, k;
  if (!wfs) return;
  for (i = 0; i < n; i++) {
    free(wfs[i].name);
    free(wfs[i].description);
    if (wfs[i].steps) {
      for (j = 0; j < wfs[i].step_count; j++) {
        workflow_step_t *s = &wfs[i].steps[j];
        free(s->id);
        free(s->tool);
        free(s->args_json);
        free(s->prompt);
        if (s->over_ids) {
          for (k = 0; k < s->over_count; k++) free(s->over_ids[k]);
          free(s->over_ids);
        }
        if (s->depends_on) {
          for (k = 0; k < s->depends_count; k++) free(s->depends_on[k]);
          free(s->depends_on);
        }
        free(s->route_on);
        free(s->route_match);
        if (s->route_then) {
          for (k = 0; k < s->route_then_count; k++) free(s->route_then[k]);
          free(s->route_then);
        }
        if (s->route_else) {
          for (k = 0; k < s->route_else_count; k++) free(s->route_else[k]);
          free(s->route_else);
        }
      }
      free(wfs[i].steps);
    }
  }
  free(wfs);
}

static int wf_id_exists(const workflow_t *wf, const char *id) {
  int m;
  if (!id) return 0;
  for (m = 0; m < wf->step_count; m++) {
    if (wf->steps[m].id && strcmp(wf->steps[m].id, id) == 0) return 1;
  }
  return 0;
}

static int validate_workflows(agent_config_t *c) {
  int i, j, k, m;
  for (i = 0; i < c->workflow_count; i++) {
    workflow_t *wf = &c->workflows[i];
    if (!wf->name || !wf->name[0]) {
      fprintf(stderr, "neo: workflows[%d]: empty name\n", i);
      return -1;
    }
    for (j = 0; j < i; j++) {
      if (c->workflows[j].name && strcmp(c->workflows[j].name, wf->name) == 0) {
        fprintf(stderr, "neo: workflows: duplicate name '%s'\n", wf->name);
        return -1;
      }
    }
    for (j = 0; j < wf->step_count; j++) {
      workflow_step_t *s = &wf->steps[j];
      if (!s->id || !s->id[0]) {
        fprintf(stderr, "neo: workflow '%s' step %d: empty id\n", wf->name, j);
        return -1;
      }
      for (k = 0; k < j; k++) {
        if (wf->steps[k].id && strcmp(wf->steps[k].id, s->id) == 0) {
          fprintf(stderr, "neo: workflow '%s': duplicate step id '%s'\n", wf->name, s->id);
          return -1;
        }
      }
      for (k = 0; k < s->depends_count; k++) {
        if (!s->depends_on[k] || !wf_id_exists(wf, s->depends_on[k])) {
          fprintf(stderr, "neo: workflow '%s' step '%s': unknown depends_on '%s'\n",
                  wf->name, s->id, s->depends_on[k] ? s->depends_on[k] : "?");
          return -1;
        }
        if (strcmp(s->depends_on[k], s->id) == 0) {
          fprintf(stderr, "neo: workflow '%s' step '%s': depends_on self\n", wf->name, s->id);
          return -1;
        }
      }
      if (s->type == WF_STEP_TOOL) {
        /* Planner often omits type: llm and only sets prompt — coerce. */
        if ((!s->tool || !s->tool[0]) && s->prompt && s->prompt[0]) {
          s->type = WF_STEP_LLM;
        } else if (!s->tool || !s->tool[0]) {
          fprintf(stderr, "neo: workflow '%s' step '%s': tool required\n", wf->name, s->id);
          return -1;
        }
      }
      if (s->type == WF_STEP_LLM) {
        if (!s->prompt || !s->prompt[0]) {
          fprintf(stderr, "neo: workflow '%s' step '%s': prompt required\n", wf->name, s->id);
          return -1;
        }
      } else if (s->type == WF_STEP_LOOP) {
        if (s->max_iters < 1) {
          fprintf(stderr, "neo: workflow '%s' step '%s': loop max must be >= 1\n", wf->name, s->id);
          return -1;
        }
        if (s->over_count < 1) {
          fprintf(stderr, "neo: workflow '%s' step '%s': loop over empty\n", wf->name, s->id);
          return -1;
        }
        for (k = 0; k < s->over_count; k++) {
          int found = 0;
          if (!s->over_ids[k]) continue;
          if (strcmp(s->over_ids[k], s->id) == 0) {
            fprintf(stderr, "neo: workflow '%s' step '%s': loop cannot include self\n", wf->name, s->id);
            return -1;
          }
          for (m = 0; m < wf->step_count; m++) {
            if (wf->steps[m].id && strcmp(wf->steps[m].id, s->over_ids[k]) == 0) {
              if (wf->steps[m].type == WF_STEP_LOOP) {
                fprintf(stderr, "neo: workflow '%s': loop over cannot include loop step '%s'\n",
                        wf->name, s->over_ids[k]);
                return -1;
              }
              found = 1;
              break;
            }
          }
          if (!found) {
            fprintf(stderr, "neo: workflow '%s' step '%s': unknown over id '%s'\n",
                    wf->name, s->id, s->over_ids[k]);
            return -1;
          }
        }
      } else if (s->type == WF_STEP_ROUTE) {
        if (!s->route_on || !s->route_on[0]) {
          fprintf(stderr, "neo: workflow '%s' step '%s': route on required\n", wf->name, s->id);
          return -1;
        }
        if (s->route_then_count < 1 && s->route_else_count < 1) {
          fprintf(stderr, "neo: workflow '%s' step '%s': route then/else empty\n", wf->name, s->id);
          return -1;
        }
        for (k = 0; k < s->route_then_count; k++) {
          if (!wf_id_exists(wf, s->route_then[k])) {
            fprintf(stderr, "neo: workflow '%s' step '%s': unknown then id\n", wf->name, s->id);
            return -1;
          }
        }
        for (k = 0; k < s->route_else_count; k++) {
          if (!wf_id_exists(wf, s->route_else[k])) {
            fprintf(stderr, "neo: workflow '%s' step '%s': unknown else id\n", wf->name, s->id);
            return -1;
          }
        }
      }
    }
  }
  return 0;
}

const workflow_t *config_find_workflow(const agent_config_t *c, const char *name) {
  int i;
  if (!c || !name) return NULL;
  for (i = 0; i < c->workflow_count; i++) {
    if (c->workflows[i].name && strcmp(c->workflows[i].name, name) == 0)
      return &c->workflows[i];
  }
  return NULL;
}

static int validate_tool_commands(agent_config_t *c) {
  int i, j;
  for (i = 0; i < c->tools.command_count; i++) {
    tool_command_t *cmd = &c->tools.commands[i];
    if (!cmd->name || !cmd->name[0]) {
      fprintf(stderr, "neo: tools.commands[%d]: empty name\n", i);
      return -1;
    }
    if (tool_command_name_reserved(cmd->name)) {
      fprintf(stderr, "neo: tools.commands: name '%s' is reserved\n", cmd->name);
      return -1;
    }
    if (cmd->argv_count < 1 || !cmd->argv || !cmd->argv[0] || !cmd->argv[0][0]) {
      fprintf(stderr, "neo: tools.commands '%s': empty argv\n", cmd->name);
      return -1;
    }
    for (j = 0; j < i; j++) {
      if (c->tools.commands[j].name && strcmp(c->tools.commands[j].name, cmd->name) == 0) {
        fprintf(stderr, "neo: tools.commands: duplicate name '%s'\n", cmd->name);
        return -1;
      }
    }
    if (cmd->timeout_sec <= 0) cmd->timeout_sec = 30;
    if (cmd->max_output_bytes <= 0) cmd->max_output_bytes = 65536;
  }
  return 0;
}

void config_init(agent_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->soul.max_chars = 8000;
  c->rules.max_chars_per_file = 8000;
  c->tools.max_rounds = 16;
  c->tools.max_read_bytes = 262144;
  c->tools.list_dir_max_entries = 256;
  c->tools.http_fetch_enabled = 0;
  c->tools.http_fetch_max_bytes = 262144;
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
  free(c->soul.path);
  c->soul.path = NULL;
  free_path_list(c->rules.paths, c->rules.path_count);
  c->rules.paths = NULL;
  c->rules.path_count = 0;
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
  free(c->tools.root);
  c->tools.root = NULL;
  free(c->tools.http_allow_hosts);
  c->tools.http_allow_hosts = NULL;
  free_tool_commands(c->tools.commands, c->tools.command_count);
  c->tools.commands = NULL;
  c->tools.command_count = 0;
  free_workflows(c->workflows, c->workflow_count);
  c->workflows = NULL;
  c->workflow_count = 0;
}

static int path_has_ext(const char *path, const char *ext) {
  size_t lp, le;
  if (!path || !ext) return 0;
  lp = strlen(path);
  le = strlen(ext);
  if (lp < le) return 0;
  return strcmp(path + lp - le, ext) == 0;
}

static char *yy_dup_str(yyjson_val *v) {
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

static int yy_bool(yyjson_val *v, int *out) {
  if (yyjson_is_bool(v)) {
    *out = yyjson_get_bool(v) ? 1 : 0;
    return 0;
  }
  if (yyjson_is_str(v)) {
    const char *s = yyjson_get_str(v);
    if (!s) return -1;
    if (!strcmp(s, "true") || !strcmp(s, "yes") || !strcmp(s, "on") || !strcmp(s, "1")) {
      *out = 1;
      return 0;
    }
    if (!strcmp(s, "false") || !strcmp(s, "no") || !strcmp(s, "off") || !strcmp(s, "0")) {
      *out = 0;
      return 0;
    }
  }
  return -1;
}

static int yy_string_array(yyjson_val *arr, char ***out, int *out_n, int max_n, const char *jpath) {
  size_t i, n;
  char **argv = NULL;
  int count = 0;
  *out = NULL;
  *out_n = 0;
  if (!yyjson_is_arr(arr)) {
    fprintf(stderr, "neo: config error at %s: expected array\n", jpath);
    return -1;
  }
  n = yyjson_arr_size(arr);
  for (i = 0; i < n; i++) {
    yyjson_val *el = yyjson_arr_get(arr, i);
    char *dup;
    char **np;
    if (!yyjson_is_str(el)) {
      fprintf(stderr, "neo: config error at %s/%zu: expected string\n", jpath, i);
      free_path_list(argv, count);
      return -1;
    }
    if (count >= max_n) {
      fprintf(stderr, "neo: config error at %s: too many entries\n", jpath);
      free_path_list(argv, count);
      return -1;
    }
    dup = yy_dup_str(el);
    if (!dup) {
      free_path_list(argv, count);
      return -1;
    }
    np = realloc(argv, (count + 1) * sizeof(char *));
    if (!np) {
      free(dup);
      free_path_list(argv, count);
      return -1;
    }
    argv = np;
    argv[count++] = dup;
  }
  *out = argv;
  *out_n = count;
  return 0;
}

static void cfg_set_str(char **dst, yyjson_val *obj, const char *key) {
  yyjson_val *v = yyjson_obj_get(obj, key);
  char *s;
  if (!v) return;
  s = yy_dup_str(v);
  if (!s) return;
  free(*dst);
  *dst = s;
}

static int fill_model(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /model: expected object\n");
    return -1;
  }
  cfg_set_str(&c->model.provider, obj, "provider");
  cfg_set_str(&c->model.base_url, obj, "base_url");
  cfg_set_str(&c->model.name, obj, "name");
  cfg_set_str(&c->model.api_key, obj, "api_key");
  v = yyjson_obj_get(obj, "max_tokens");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->model.max_tokens = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "temperature");
  if (yyjson_is_num(v)) c->model.temperature = yyjson_get_real(v);
  return 0;
}

static int fill_memory(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /memory: expected object\n");
    return -1;
  }
  cfg_set_str(&c->memory.path, obj, "path");
  v = yyjson_obj_get(obj, "max_chars");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->memory.max_chars = (int)yyjson_get_sint(v);
  return 0;
}

static int fill_soul(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /soul: expected object\n");
    return -1;
  }
  cfg_set_str(&c->soul.path, obj, "path");
  v = yyjson_obj_get(obj, "max_chars");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->soul.max_chars = (int)yyjson_get_sint(v);
  return 0;
}

static int fill_workspace(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  int b;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /workspace: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "prompt_cwd");
  if (v && yy_bool(v, &b) == 0) c->workspace.prompt_cwd = b;
  return 0;
}

static int fill_session(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /session: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "max_turns");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->session_max_turns = (int)yyjson_get_sint(v);
  return 0;
}

static int fill_plan(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /plan: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "target_steps");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->plan.target_steps = (int)yyjson_get_sint(v);
  return 0;
}

static int fill_paths_section(yyjson_val *obj, char ***paths, int *count, int *max_chars,
                              const char *section) {
  yyjson_val *v, *arr;
  char pathbuf[64];
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /%s: expected object\n", section);
    return -1;
  }
  v = yyjson_obj_get(obj, "max_chars_per_file");
  if (max_chars && (yyjson_is_int(v) || yyjson_is_uint(v)))
    *max_chars = (int)yyjson_get_sint(v);
  arr = yyjson_obj_get(obj, "paths");
  if (!arr) return 0;
  snprintf(pathbuf, sizeof(pathbuf), "/%s/paths", section);
  return yy_string_array(arr, paths, count, MAX_PATHS, pathbuf);
}

static int fill_skills(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v, *arr, *el;
  size_t i, n;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /skills: expected object\n");
    return -1;
  }
  cfg_set_str(&c->skills.directory, obj, "directory");
  v = yyjson_obj_get(obj, "unmatched");
  if (yyjson_is_str(v)) {
    const char *s = yyjson_get_str(v);
    if (s && !strcmp(s, "skip")) c->skills.unmatched = 1;
    else c->skills.unmatched = 0;
  }
  arr = yyjson_obj_get(obj, "high_priority");
  if (arr) {
    if (yy_string_array(arr, &c->skills.high_priority, &c->skills.high_priority_count, MAX_PATHS,
                        "/skills/high_priority") != 0)
      return -1;
  }
  arr = yyjson_obj_get(obj, "paths");
  if (!arr) return 0;
  if (!yyjson_is_arr(arr)) {
    fprintf(stderr, "neo: config error at /skills/paths: expected array\n");
    return -1;
  }
  n = yyjson_arr_size(arr);
  for (i = 0; i < n; i++) {
    yyjson_val *path_v, *pri_v;
    char *dup;
    int *np;
    int pri = 0;
    el = yyjson_arr_get(arr, i);
    if (yyjson_is_str(el)) {
      path_v = el;
    } else if (yyjson_is_obj(el)) {
      path_v = yyjson_obj_get(el, "path");
      pri_v = yyjson_obj_get(el, "priority");
      if (yyjson_is_str(pri_v)) {
        const char *ps = yyjson_get_str(pri_v);
        if (ps && (!strcmp(ps, "high") || !strcmp(ps, "1"))) pri = 1;
      } else if (yyjson_is_int(pri_v) && yyjson_get_sint(pri_v) == 1)
        pri = 1;
    } else {
      fprintf(stderr, "neo: config error at /skills/paths/%zu: expected string or object\n", i);
      return -1;
    }
    if (!yyjson_is_str(path_v)) {
      fprintf(stderr, "neo: config error at /skills/paths/%zu: missing path\n", i);
      return -1;
    }
    if (c->skills.path_count >= MAX_PATHS) {
      fprintf(stderr, "neo: config error at /skills/paths: too many entries\n");
      return -1;
    }
    dup = yy_dup_str(path_v);
    if (!dup) return -1;
    {
      char **pp = realloc(c->skills.paths, (c->skills.path_count + 1) * sizeof(char *));
      if (!pp) {
        free(dup);
        return -1;
      }
      c->skills.paths = pp;
      c->skills.paths[c->skills.path_count] = dup;
    }
    np = realloc(c->skills.priority, (c->skills.path_count + 1) * sizeof(int));
    if (!np) return -1;
    c->skills.priority = np;
    c->skills.priority[c->skills.path_count] = pri;
    c->skills.path_count++;
  }
  return 0;
}

static int fill_tools(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v, *cmds, *el;
  size_t i, n;
  int b;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /tools: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "enabled");
  if (v && yy_bool(v, &b) == 0) c->tools.enabled = b;
  cfg_set_str(&c->tools.root, obj, "root");
  v = yyjson_obj_get(obj, "max_rounds");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.max_rounds = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "max_read_bytes");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.max_read_bytes = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "list_dir_max_entries");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.list_dir_max_entries = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "http_fetch_enabled");
  if (v && yy_bool(v, &b) == 0) c->tools.http_fetch_enabled = b;
  cfg_set_str(&c->tools.http_allow_hosts, obj, "http_allow_hosts");
  v = yyjson_obj_get(obj, "http_fetch_max_bytes");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.http_fetch_max_bytes = (int)yyjson_get_sint(v);

  cmds = yyjson_obj_get(obj, "commands");
  if (!cmds) return 0;
  if (!yyjson_is_arr(cmds)) {
    fprintf(stderr, "neo: config error at /tools/commands: expected array\n");
    return -1;
  }
  n = yyjson_arr_size(cmds);
  for (i = 0; i < n; i++) {
    tool_command_t *cmd;
    tool_command_t *np;
    yyjson_val *argv, *pa;
    char pathbuf[64];
    el = yyjson_arr_get(cmds, i);
    if (!yyjson_is_obj(el)) {
      fprintf(stderr, "neo: config error at /tools/commands/%zu: expected object\n", i);
      return -1;
    }
    if (c->tools.command_count >= MAX_COMMANDS) {
      fprintf(stderr, "neo: tools.commands: too many entries\n");
      return -1;
    }
    np = realloc(c->tools.commands, (c->tools.command_count + 1) * sizeof(tool_command_t));
    if (!np) return -1;
    c->tools.commands = np;
    cmd = &c->tools.commands[c->tools.command_count];
    memset(cmd, 0, sizeof(*cmd));
    cmd->name = yy_dup_str(yyjson_obj_get(el, "name"));
    cmd->description = yy_dup_str(yyjson_obj_get(el, "description"));
    v = yyjson_obj_get(el, "timeout_sec");
    if (yyjson_is_int(v) || yyjson_is_uint(v)) cmd->timeout_sec = (int)yyjson_get_sint(v);
    v = yyjson_obj_get(el, "max_output_bytes");
    if (yyjson_is_int(v) || yyjson_is_uint(v)) cmd->max_output_bytes = (int)yyjson_get_sint(v);
    pa = yyjson_obj_get(el, "pass_args");
    cmd->pass_args = 0;
    if (yyjson_is_str(pa) && yyjson_get_str(pa) && !strcmp(yyjson_get_str(pa), "env"))
      cmd->pass_args = 1;
    argv = yyjson_obj_get(el, "argv");
    snprintf(pathbuf, sizeof(pathbuf), "/tools/commands/%zu/argv", i);
    if (argv && yy_string_array(argv, &cmd->argv, &cmd->argv_count, MAX_ARGV, pathbuf) != 0)
      return -1;
    c->tools.command_count++;
  }
  return 0;
}

static wf_step_type_t parse_step_type(const char *s) {
  if (!s) return WF_STEP_TOOL;
  if (!strcmp(s, "llm")) return WF_STEP_LLM;
  if (!strcmp(s, "loop")) return WF_STEP_LOOP;
  if (!strcmp(s, "route")) return WF_STEP_ROUTE;
  return WF_STEP_TOOL;
}

static int fill_workflows(agent_config_t *c, yyjson_val *arr) {
  size_t wi, wn;
  if (!yyjson_is_arr(arr)) {
    fprintf(stderr, "neo: config error at /workflows: expected array\n");
    return -1;
  }
  wn = yyjson_arr_size(arr);
  for (wi = 0; wi < wn; wi++) {
    yyjson_val *wobj, *steps, *st;
    workflow_t *wf;
    workflow_t *np;
    size_t si, sn;
    wobj = yyjson_arr_get(arr, wi);
    if (!yyjson_is_obj(wobj)) {
      fprintf(stderr, "neo: config error at /workflows/%zu: expected object\n", wi);
      return -1;
    }
    if (c->workflow_count >= MAX_COMMANDS) {
      fprintf(stderr, "neo: workflows: too many entries\n");
      return -1;
    }
    np = realloc(c->workflows, (c->workflow_count + 1) * sizeof(workflow_t));
    if (!np) return -1;
    c->workflows = np;
    wf = &c->workflows[c->workflow_count];
    memset(wf, 0, sizeof(*wf));
    wf->name = yy_dup_str(yyjson_obj_get(wobj, "name"));
    wf->description = yy_dup_str(yyjson_obj_get(wobj, "description"));
    steps = yyjson_obj_get(wobj, "steps");
    if (!steps) {
      c->workflow_count++;
      continue;
    }
    if (!yyjson_is_arr(steps)) {
      fprintf(stderr, "neo: config error at /workflows/%zu/steps: expected array\n", wi);
      return -1;
    }
    sn = yyjson_arr_size(steps);
    for (si = 0; si < sn; si++) {
      workflow_step_t *s;
      workflow_step_t *snp;
      yyjson_val *type_v, *tools_v, *args_v, *max_v, *dep, *over, *then_a, *else_a;
      char pathbuf[80];
      st = yyjson_arr_get(steps, si);
      if (!yyjson_is_obj(st)) {
        fprintf(stderr, "neo: config error at /workflows/%zu/steps/%zu: expected object\n", wi, si);
        return -1;
      }
      if (wf->step_count >= MAX_PATHS) {
        fprintf(stderr, "neo: workflow steps: too many\n");
        return -1;
      }
      snp = realloc(wf->steps, (wf->step_count + 1) * sizeof(workflow_step_t));
      if (!snp) return -1;
      wf->steps = snp;
      s = &wf->steps[wf->step_count];
      memset(s, 0, sizeof(*s));
      s->tools_on = 1;
      s->id = yy_dup_str(yyjson_obj_get(st, "id"));
      type_v = yyjson_obj_get(st, "type");
      if (yyjson_is_str(type_v))
        s->type = parse_step_type(yyjson_get_str(type_v));
      else
        s->type = WF_STEP_TOOL;
      s->tool = yy_dup_str(yyjson_obj_get(st, "tool"));
      s->prompt = yy_dup_str(yyjson_obj_get(st, "prompt"));
      if (!yyjson_is_str(type_v) && s->prompt && s->prompt[0] && (!s->tool || !s->tool[0]))
        s->type = WF_STEP_LLM;
      tools_v = yyjson_obj_get(st, "tools");
      if (yyjson_is_str(tools_v)) {
        const char *ts = yyjson_get_str(tools_v);
        if (ts && (!strcmp(ts, "off") || !strcmp(ts, "false") || !strcmp(ts, "0")))
          s->tools_on = 0;
      } else if (yyjson_is_bool(tools_v)) {
        s->tools_on = yyjson_get_bool(tools_v) ? 1 : 0;
      }
      args_v = yyjson_obj_get(st, "args");
      if (args_v) s->args_json = yyjson_val_write(args_v, 0, NULL);
      max_v = yyjson_obj_get(st, "max");
      if (yyjson_is_int(max_v) || yyjson_is_uint(max_v)) s->max_iters = (int)yyjson_get_sint(max_v);
      s->route_on = yy_dup_str(yyjson_obj_get(st, "on"));
      s->route_match = yy_dup_str(yyjson_obj_get(st, "match"));
      dep = yyjson_obj_get(st, "depends_on");
      if (dep) {
        snprintf(pathbuf, sizeof(pathbuf), "/workflows/%zu/steps/%zu/depends_on", wi, si);
        if (yy_string_array(dep, &s->depends_on, &s->depends_count, MAX_ARGV, pathbuf) != 0)
          return -1;
      }
      over = yyjson_obj_get(st, "over");
      if (over) {
        snprintf(pathbuf, sizeof(pathbuf), "/workflows/%zu/steps/%zu/over", wi, si);
        if (yy_string_array(over, &s->over_ids, &s->over_count, MAX_ARGV, pathbuf) != 0)
          return -1;
      }
      then_a = yyjson_obj_get(st, "then");
      if (then_a) {
        snprintf(pathbuf, sizeof(pathbuf), "/workflows/%zu/steps/%zu/then", wi, si);
        if (yy_string_array(then_a, &s->route_then, &s->route_then_count, MAX_ARGV, pathbuf) != 0)
          return -1;
      }
      else_a = yyjson_obj_get(st, "else");
      if (else_a) {
        snprintf(pathbuf, sizeof(pathbuf), "/workflows/%zu/steps/%zu/else", wi, si);
        if (yy_string_array(else_a, &s->route_else, &s->route_else_count, MAX_ARGV, pathbuf) != 0)
          return -1;
      }
      wf->step_count++;
    }
    c->workflow_count++;
  }
  return 0;
}

static void scan_skills_directory(agent_config_t *c) {
#if defined(__linux__) || defined(__APPLE__)
  if (c->skills.directory && c->skills.directory[0]) {
    DIR *dir = opendir(c->skills.directory);
    if (dir) {
      char **scanned = NULL;
      int n_scan = 0;
      struct dirent *e;
      while (n_scan < MAX_PATHS && (e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        {
          char subpath[1024];
          struct stat st;
          snprintf(subpath, sizeof(subpath), "%s/%s/SKILL.md", c->skills.directory, e->d_name);
          if (stat(subpath, &st) == 0 && S_ISREG(st.st_mode)) {
            char *dup = dup_str(subpath);
            if (dup) {
              char **np = realloc(scanned, (n_scan + 1) * sizeof(char *));
              if (np) {
                scanned = np;
                scanned[n_scan++] = dup;
              } else
                free(dup);
            }
          }
        }
      }
      closedir(dir);
      if (n_scan > 0) {
        char **new_paths = malloc((size_t)(n_scan + c->skills.path_count) * sizeof(char *));
        int *new_pri = malloc((size_t)(n_scan + c->skills.path_count) * sizeof(int));
        if (new_paths && new_pri) {
          int i;
          for (i = 0; i < n_scan; i++) {
            new_paths[i] = scanned[i];
            new_pri[i] = 0;
          }
          for (i = 0; i < c->skills.path_count; i++) {
            new_paths[n_scan + i] = c->skills.paths[i];
            new_pri[n_scan + i] = c->skills.priority ? c->skills.priority[i] : 0;
          }
          free(c->skills.paths);
          free(c->skills.priority);
          c->skills.paths = new_paths;
          c->skills.priority = new_pri;
          c->skills.path_count = n_scan + c->skills.path_count;
        } else {
          free(new_paths);
          free(new_pri);
          free_path_list(scanned, n_scan);
        }
      } else
        free_path_list(scanned, n_scan);
    }
  }
  if (c->skills.high_priority_count > 0 && c->skills.priority) {
    int i, j;
    for (i = 0; i < c->skills.path_count; i++) {
      const char *spath = c->skills.paths[i];
      const char *last_slash = spath ? strrchr(spath, '/') : NULL;
      const char *name = spath;
      size_t namelen = 0;
      if (last_slash && last_slash > spath) {
        const char *prev = last_slash;
        while (prev > spath && prev[-1] != '/') prev--;
        if (prev < last_slash) {
          name = prev;
          if (*name == '/') name++;
          namelen = (size_t)(last_slash - name);
        }
      }
      for (j = 0; j < c->skills.high_priority_count; j++) {
        const char *hp = c->skills.high_priority[j];
        if (!hp) continue;
        if ((spath && strstr(spath, hp)) ||
            (namelen > 0 && strlen(hp) == namelen && strncmp(name, hp, namelen) == 0) ||
            (spath && !strcmp(spath, hp))) {
          c->skills.priority[i] = 1;
          break;
        }
      }
    }
  }
#else
  (void)c;
#endif
}

int config_load_file(agent_config_t *c, const char *path) {
  yyjson_read_err err;
  yyjson_doc *doc;
  yyjson_val *root, *sec;

  if (!c || !path) return -1;
  if (path_has_ext(path, ".yaml") || path_has_ext(path, ".yml")) {
    fprintf(stderr,
            "neo: config is JSON5-only; migrate to .json5 (see docs/migrate-json.md)\n");
    return -1;
  }

  c->memory.max_chars = 4000;
  c->model.max_tokens = 4096;
  c->model.temperature = 0.7;
  c->bootstrap.max_chars_per_file = 8000;
  c->session_max_turns = 10;

  memset(&err, 0, sizeof(err));
  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: JSON5 parse error in %s: %s (at %zu)\n", path,
            err.msg ? err.msg : "unknown", (size_t)err.pos);
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    fprintf(stderr, "neo: config root must be a JSON object\n");
    yyjson_doc_free(doc);
    return -1;
  }

  if ((sec = yyjson_obj_get(root, "model")) && fill_model(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "memory")) && fill_memory(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "soul")) && fill_soul(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "workspace")) && fill_workspace(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "session")) && fill_session(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "plan")) && fill_plan(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "bootstrap")) &&
      fill_paths_section(sec, &c->bootstrap.paths, &c->bootstrap.path_count,
                         &c->bootstrap.max_chars_per_file, "bootstrap") != 0)
    goto fail;
  if ((sec = yyjson_obj_get(root, "rules")) &&
      fill_paths_section(sec, &c->rules.paths, &c->rules.path_count, &c->rules.max_chars_per_file,
                         "rules") != 0)
    goto fail;
  if ((sec = yyjson_obj_get(root, "skills")) && fill_skills(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "tools")) && fill_tools(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "workflows")) && fill_workflows(c, sec) != 0) goto fail;

  yyjson_doc_free(doc);

  if (c->session_max_turns <= 0) c->session_max_turns = 10;
  if (c->tools.list_dir_max_entries <= 0) c->tools.list_dir_max_entries = 256;
  if (c->tools.http_fetch_max_bytes <= 0) c->tools.http_fetch_max_bytes = 262144;
  if (c->soul.max_chars <= 0) c->soul.max_chars = 8000;
  if (c->rules.max_chars_per_file <= 0) c->rules.max_chars_per_file = 8000;
  if (validate_tool_commands(c) != 0) return -1;
  if (validate_workflows(c) != 0) return -1;

  scan_skills_directory(c);

  if (!c->model.base_url) c->model.base_url = dup_str("http://127.0.0.1:11434/v1");
  if (!c->model.name) c->model.name = dup_str("qwen3:8b");
  if (!c->model.provider) c->model.provider = dup_str("ollama");
  if (c->model.max_tokens <= 0) c->model.max_tokens = 4096;
  return 0;

fail:
  yyjson_doc_free(doc);
  return -1;
}

void config_apply_env(agent_config_t *c) {
  const char *v;
  v = getenv("NEO_MODEL");
  if (v && v[0]) {
    free(c->model.name);
    c->model.name = dup_str(v);
  }
  v = getenv("NEO_API_KEY");
  if (v && v[0]) {
    free(c->model.api_key);
    c->model.api_key = dup_str(v);
  }
  v = getenv("NEO_CONFIG");
  (void)v;
}

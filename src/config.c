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
#define MAX_COMMANDS 32
#define MAX_ARGV 16

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

/* Parse argv: ["a","b"] or [a, b] into newly allocated argv list. Returns count or -1. */
static int parse_bracket_list(const char *s, char ***out_argv, int max_n) {
  char **argv = NULL;
  int n = 0;
  if (!s || !out_argv) return -1;
  *out_argv = NULL;
  while (*s == ' ' || *s == '\t') s++;
  if (*s != '[') return -1;
  s++;
  while (*s) {
    char buf[MAX_STR];
    size_t len = 0;
    while (*s == ' ' || *s == '\t' || *s == ',') s++;
    if (*s == ']') break;
    if (*s == '"') {
      s++;
      while (*s && *s != '"' && len + 1 < sizeof(buf)) buf[len++] = *s++;
      if (*s == '"') s++;
    } else {
      while (*s && *s != ',' && *s != ']' && *s != ' ' && *s != '\t' && len + 1 < sizeof(buf))
        buf[len++] = *s++;
    }
    buf[len] = '\0';
    if (len == 0) continue;
    if (n >= max_n) {
      free_path_list(argv, n);
      return -1;
    }
    {
      char *dup = dup_str(buf);
      char **np;
      if (!dup) { free_path_list(argv, n); return -1; }
      np = realloc(argv, (n + 1) * sizeof(char *));
      if (!np) { free(dup); free_path_list(argv, n); return -1; }
      argv = np;
      argv[n++] = dup;
    }
  }
  *out_argv = argv;
  return n;
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
      }
      free(wfs[i].steps);
    }
  }
  free(wfs);
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
      if (s->type == WF_STEP_TOOL) {
        if (!s->tool || !s->tool[0]) {
          fprintf(stderr, "neo: workflow '%s' step '%s': tool required\n", wf->name, s->id);
          return -1;
        }
      } else if (s->type == WF_STEP_LLM) {
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
  int in_tools = 0, in_soul = 0, in_rules = 0, in_workspace = 0, in_commands = 0, in_workflows = 0;
  int in_wf_steps = 0;
  c->memory.max_chars = 4000;
  c->model.max_tokens = 4096;
  c->model.temperature = 0.7;
  c->bootstrap.max_chars_per_file = 8000;
  c->session_max_turns = 10;

  while (fgets(line, sizeof(line), f)) {
    char *t = line;
    while (*t == ' ' || *t == '\t') t++;
    if (*t == '#' || *t == '\n' || *t == '\0') continue;

    if (strncmp(t, "model:", 6) == 0) {
      in_model = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_skills = in_memory = in_bootstrap = in_session = in_tools = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "skills:", 7) == 0) {
      in_skills = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_memory = in_bootstrap = in_session = in_tools = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "memory:", 7) == 0) {
      in_memory = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_bootstrap = in_session = in_tools = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "bootstrap:", 10) == 0) {
      in_bootstrap = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_memory = in_session = in_tools = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "session:", 8) == 0) {
      in_session = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_memory = in_bootstrap = in_tools = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "tools:", 6) == 0) {
      in_tools = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_memory = in_bootstrap = in_session = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "workflows:", 10) == 0) {
      in_workflows = 1;
      in_wf_steps = 0;
      in_high_priority = 0;
      in_commands = 0;
      in_model = in_skills = in_memory = in_bootstrap = in_session = in_tools = in_soul = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "soul:", 5) == 0) {
      in_soul = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_memory = in_bootstrap = in_session = in_tools = in_rules = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "rules:", 6) == 0) {
      in_rules = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_memory = in_bootstrap = in_session = in_tools = in_soul = in_workspace = 0;
      continue;
    }
    if (strncmp(t, "workspace:", 10) == 0) {
      in_workspace = 1;
      in_high_priority = 0;
      in_commands = 0;
      in_workflows = 0;
      in_wf_steps = 0;
      in_model = in_skills = in_memory = in_bootstrap = in_session = in_tools = in_soul = in_rules = 0;
      continue;
    }

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
    if (in_soul) {
      if (strncmp(t, "path:", 5) == 0) {
        free(c->soul.path);
        c->soul.path = dup_str(trim_quotes(t + 5));
      } else if (strncmp(t, "max_chars:", 10) == 0)
        c->soul.max_chars = atoi(t + 10);
    }
    if (in_rules) {
      if (strncmp(t, "- path:", 7) == 0)
        add_path(&c->rules.paths, &c->rules.path_count, trim_quotes(t + 7), MAX_PATHS);
      else if (strncmp(t, "max_chars_per_file:", 19) == 0)
        c->rules.max_chars_per_file = atoi(t + 19);
    }
    if (in_workspace) {
      if (strncmp(t, "prompt_cwd:", 11) == 0) {
        const char *v = trim_quotes(t + 11);
        while (*v == ' ' || *v == '\t') v++;
        c->workspace.prompt_cwd =
            (strncmp(v, "true", 4) == 0 || strncmp(v, "yes", 3) == 0 || strncmp(v, "on", 2) == 0 || strcmp(v, "1") == 0) ? 1 : 0;
      }
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

    if (in_workflows) {
      if (strncmp(t, "- name:", 7) == 0) {
        workflow_t *np;
        workflow_t *wf;
        if (c->workflow_count >= MAX_COMMANDS) {
          fprintf(stderr, "neo: workflows: too many entries\n");
          fclose(f);
          return -1;
        }
        np = realloc(c->workflows, (c->workflow_count + 1) * sizeof(workflow_t));
        if (!np) { fclose(f); return -1; }
        c->workflows = np;
        wf = &c->workflows[c->workflow_count];
        memset(wf, 0, sizeof(*wf));
        wf->name = dup_str(trim_quotes(t + 7));
        c->workflow_count++;
        in_wf_steps = 0;
        continue;
      }
      if (c->workflow_count > 0) {
        workflow_t *wf = &c->workflows[c->workflow_count - 1];
        if (strncmp(t, "description:", 12) == 0) {
          free(wf->description);
          wf->description = dup_str(trim_quotes(t + 12));
          continue;
        }
        if (strncmp(t, "steps:", 6) == 0) {
          in_wf_steps = 1;
          continue;
        }
        if (in_wf_steps && strncmp(t, "- id:", 5) == 0) {
          workflow_step_t *sp;
          workflow_step_t *st;
          if (wf->step_count >= MAX_PATHS) {
            fprintf(stderr, "neo: workflow '%s': too many steps\n", wf->name ? wf->name : "?");
            fclose(f);
            return -1;
          }
          sp = realloc(wf->steps, (wf->step_count + 1) * sizeof(workflow_step_t));
          if (!sp) { fclose(f); return -1; }
          wf->steps = sp;
          st = &wf->steps[wf->step_count];
          memset(st, 0, sizeof(*st));
          st->id = dup_str(trim_quotes(t + 5));
          st->type = WF_STEP_TOOL;
          wf->step_count++;
          continue;
        }
        if (in_wf_steps && wf->step_count > 0) {
          workflow_step_t *st = &wf->steps[wf->step_count - 1];
          if (strncmp(t, "type:", 5) == 0) {
            const char *v = trim_quotes(t + 5);
            while (*v == ' ' || *v == '\t') v++;
            if (strncmp(v, "llm", 3) == 0) st->type = WF_STEP_LLM;
            else if (strncmp(v, "loop", 4) == 0) st->type = WF_STEP_LOOP;
            else st->type = WF_STEP_TOOL;
            continue;
          }
          if (strncmp(t, "tools:", 6) == 0) {
            const char *v = trim_quotes(t + 6);
            while (*v == ' ' || *v == '\t') v++;
            st->tools_on = (strncmp(v, "on", 2) == 0) ? 1 : 0;
            continue;
          }
          if (strncmp(t, "tool:", 5) == 0) {
            free(st->tool);
            st->tool = dup_str(trim_quotes(t + 5));
            continue;
          }
          if (strncmp(t, "args:", 5) == 0) {
            char *v = t + 5;
            while (*v == ' ' || *v == '\t') v++;
            {
              char *end = v + strlen(v);
              while (end > v && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
                end--;
              *end = '\0';
            }
            free(st->args_json);
            st->args_json = dup_str(v);
            continue;
          }
          if (strncmp(t, "prompt:", 7) == 0) {
            free(st->prompt);
            st->prompt = dup_str(trim_quotes(t + 7));
            continue;
          }
          if (strncmp(t, "over:", 5) == 0) {
            char **ids = NULL;
            int n = parse_bracket_list(t + 5, &ids, MAX_ARGV);
            if (n < 0) {
              fprintf(stderr, "neo: workflow step '%s': bad over list\n", st->id ? st->id : "?");
              fclose(f);
              return -1;
            }
            if (st->over_ids) {
              int k;
              for (k = 0; k < st->over_count; k++) free(st->over_ids[k]);
              free(st->over_ids);
            }
            st->over_ids = ids;
            st->over_count = n;
            continue;
          }
          if (strncmp(t, "max:", 4) == 0) {
            st->max_iters = atoi(t + 4);
            continue;
          }
          if (strncmp(t, "until:", 6) == 0) {
            /* first phase: only always — ignore value */
            continue;
          }
        }
      }
      continue;
    }

    if (in_tools) {
      if (strncmp(t, "commands:", 9) == 0) {
        in_commands = 1;
        continue;
      }
      if (in_commands && strncmp(t, "- name:", 7) == 0) {
        tool_command_t *np;
        tool_command_t *cmd;
        if (c->tools.command_count >= MAX_COMMANDS) {
          fprintf(stderr, "neo: tools.commands: too many entries\n");
          fclose(f);
          return -1;
        }
        np = realloc(c->tools.commands, (c->tools.command_count + 1) * sizeof(tool_command_t));
        if (!np) { fclose(f); return -1; }
        c->tools.commands = np;
        cmd = &c->tools.commands[c->tools.command_count];
        memset(cmd, 0, sizeof(*cmd));
        cmd->name = dup_str(trim_quotes(t + 7));
        cmd->timeout_sec = 30;
        cmd->max_output_bytes = 65536;
        cmd->pass_args = 0;
        c->tools.command_count++;
        continue;
      }
      if (in_commands && c->tools.command_count > 0) {
        tool_command_t *cmd = &c->tools.commands[c->tools.command_count - 1];
        if (strncmp(t, "description:", 12) == 0) {
          free(cmd->description);
          cmd->description = dup_str(trim_quotes(t + 12));
          continue;
        }
        if (strncmp(t, "argv:", 5) == 0) {
          char **argv = NULL;
          int n = parse_bracket_list(t + 5, &argv, MAX_ARGV);
          if (n < 0) {
            fprintf(stderr, "neo: tools.commands '%s': bad argv list\n",
                    cmd->name ? cmd->name : "?");
            fclose(f);
            return -1;
          }
          if (cmd->argv) {
            int j;
            for (j = 0; j < cmd->argv_count; j++) free(cmd->argv[j]);
            free(cmd->argv);
          }
          cmd->argv = argv;
          cmd->argv_count = n;
          continue;
        }
        if (strncmp(t, "timeout_sec:", 12) == 0) {
          cmd->timeout_sec = atoi(t + 12);
          continue;
        }
        if (strncmp(t, "max_output_bytes:", 17) == 0) {
          cmd->max_output_bytes = atoi(t + 17);
          continue;
        }
        if (strncmp(t, "pass_args:", 10) == 0) {
          const char *v = trim_quotes(t + 10);
          while (*v == ' ' || *v == '\t') v++;
          if (strncmp(v, "env", 3) == 0)
            cmd->pass_args = 1;
          else
            cmd->pass_args = 0;
          continue;
        }
      }
      if (strncmp(t, "enabled:", 8) == 0) {
        const char *v = trim_quotes(t + 8);
        while (*v == ' ' || *v == '\t') v++;
        c->tools.enabled = (strncmp(v, "true", 4) == 0 || strncmp(v, "yes", 3) == 0 || strncmp(v, "on", 2) == 0 || strcmp(v, "1") == 0) ? 1 : 0;
        in_commands = 0;
      } else if (strncmp(t, "root:", 5) == 0) {
        free(c->tools.root);
        c->tools.root = dup_str(trim_quotes(t + 5));
        in_commands = 0;
      } else if (strncmp(t, "max_rounds:", 11) == 0) {
        c->tools.max_rounds = atoi(t + 11);
        in_commands = 0;
      } else if (strncmp(t, "max_read_bytes:", 15) == 0) {
        c->tools.max_read_bytes = atoi(t + 15);
        in_commands = 0;
      } else if (strncmp(t, "list_dir_max_entries:", 21) == 0) {
        c->tools.list_dir_max_entries = atoi(t + 21);
        in_commands = 0;
      } else if (strncmp(t, "http_fetch_enabled:", 19) == 0) {
        const char *v = trim_quotes(t + 19);
        while (*v == ' ' || *v == '\t') v++;
        c->tools.http_fetch_enabled =
            (strncmp(v, "true", 4) == 0 || strncmp(v, "yes", 3) == 0 || strncmp(v, "on", 2) == 0 || strcmp(v, "1") == 0) ? 1 : 0;
        in_commands = 0;
      } else if (strncmp(t, "http_allow_hosts:", 17) == 0) {
        free(c->tools.http_allow_hosts);
        c->tools.http_allow_hosts = dup_str(trim_quotes(t + 17));
        in_commands = 0;
      } else if (strncmp(t, "http_fetch_max_bytes:", 21) == 0) {
        c->tools.http_fetch_max_bytes = atoi(t + 21);
        in_commands = 0;
      }
    }
  }
  fclose(f);
  if (c->session_max_turns <= 0) c->session_max_turns = 10;
  if (c->tools.list_dir_max_entries <= 0) c->tools.list_dir_max_entries = 256;
  if (c->tools.http_fetch_max_bytes <= 0) c->tools.http_fetch_max_bytes = 262144;
  if (c->soul.max_chars <= 0) c->soul.max_chars = 8000;
  if (c->rules.max_chars_per_file <= 0) c->rules.max_chars_per_file = 8000;
  if (validate_tool_commands(c) != 0) return -1;
  if (validate_workflows(c) != 0) return -1;

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

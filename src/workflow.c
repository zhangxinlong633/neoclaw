/*
 * Declarative workflow runner: tool / llm / loop (max).
 */
#include "workflow.h"
#include "agent_tools.h"
#include "llm.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

#define WF_MAX_STEPS 32

typedef struct {
  char *id;
  char *text;
} wf_out_map_t;

static void wf_map_free(wf_out_map_t *m, int n) {
  int i;
  for (i = 0; i < n; i++) {
    free(m[i].id);
    free(m[i].text);
  }
}

static void wf_map_set(wf_out_map_t *m, int *n, const char *id, const char *text) {
  int i;
  for (i = 0; i < *n; i++) {
    if (m[i].id && id && strcmp(m[i].id, id) == 0) {
      free(m[i].text);
      m[i].text = text ? strdup(text) : strdup("");
      return;
    }
  }
  if (*n >= WF_MAX_STEPS) return;
  m[*n].id = id ? strdup(id) : NULL;
  m[*n].text = text ? strdup(text) : strdup("");
  (*n)++;
}

char *workflow_expand_template(const char *tmpl, const char *prev,
                               const char **ids, const char **texts, int n_maps) {
  size_t cap = tmpl ? strlen(tmpl) + 64 : 64;
  size_t len = 0;
  char *out;
  const char *p;
  if (!tmpl) return NULL;
  out = malloc(cap);
  if (!out) return NULL;
  out[0] = '\0';
  p = tmpl;
  while (*p) {
    if (p[0] == '{' && p[1] == '{') {
      const char *end = strstr(p + 2, "}}");
      char key[128];
      size_t klen;
      const char *rep = NULL;
      if (!end) {
        free(out);
        return NULL;
      }
      klen = (size_t)(end - (p + 2));
      if (klen >= sizeof(key)) {
        free(out);
        return NULL;
      }
      memcpy(key, p + 2, klen);
      key[klen] = '\0';
      if (strcmp(key, "prev") == 0) {
        rep = prev ? prev : "";
      } else if (strncmp(key, "steps.", 6) == 0) {
        const char *sid = key + 6;
        int i;
        for (i = 0; i < n_maps; i++) {
          if (ids[i] && strcmp(ids[i], sid) == 0) {
            rep = texts[i] ? texts[i] : "";
            break;
          }
        }
        if (!rep) {
          free(out);
          return NULL;
        }
      } else {
        free(out);
        return NULL;
      }
      {
        size_t rlen = strlen(rep);
        if (len + rlen + 1 > cap) {
          size_t ncap = (len + rlen + 1) * 2;
          char *nb = realloc(out, ncap);
          if (!nb) {
            free(out);
            return NULL;
          }
          out = nb;
          cap = ncap;
        }
        memcpy(out + len, rep, rlen);
        len += rlen;
        out[len] = '\0';
      }
      p = end + 2;
      continue;
    }
    if (len + 2 > cap) {
      size_t ncap = cap * 2;
      char *nb = realloc(out, ncap);
      if (!nb) {
        free(out);
        return NULL;
      }
      out = nb;
      cap = ncap;
    }
    out[len++] = *p++;
    out[len] = '\0';
  }
  return out;
}

static int wf_run_step(const agent_config_t *conf, const char *root_real, const char *wf_name,
                       const workflow_t *wf, const workflow_step_t *st,
                       wf_out_map_t *map, int *map_n, char **prev_io);

static int wf_run_tool_step(const agent_config_t *conf, const char *root_real, const char *wf_name,
                            const workflow_step_t *st, wf_out_map_t *map, int *map_n, char **prev_io) {
  char *out = NULL;
  size_t out_len = 0;
  const char *args = (st->args_json && st->args_json[0]) ? st->args_json : "{}";
  if (!conf->tools.enabled) {
    fprintf(stderr, "neo: workflow:%s step:%s: tools disabled\n", wf_name, st->id);
    return -1;
  }
  if (neo_dispatch_tool(conf, root_real, st->tool, args, &out, &out_len) != 0) {
    fprintf(stderr, "neo: workflow:%s step:%s: tool dispatch failed\n", wf_name, st->id);
    free(out);
    return -1;
  }
  if (out && strncmp(out, "ERROR:", 6) == 0) {
    fprintf(stderr, "neo: workflow:%s step:%s: %s\n", wf_name, st->id, out);
    free(out);
    return -1;
  }
  if (out && strncmp(out, "EXIT:", 5) == 0) {
    fprintf(stderr, "neo: workflow:%s step:%s: tool non-zero exit\n", wf_name, st->id);
    free(out);
    return -1;
  }
  wf_map_set(map, map_n, st->id, out ? out : "");
  free(*prev_io);
  *prev_io = out ? strdup(out) : strdup("");
  free(out);
  return 0;
}

static int wf_run_llm_step(const agent_config_t *conf, const char *wf_name, const workflow_step_t *st,
                           wf_out_map_t *map, int *map_n, char **prev_io) {
  const char *ids[WF_MAX_STEPS];
  const char *texts[WF_MAX_STEPS];
  char *prompt = NULL;
  llm_response_t resp;
  int i;
  for (i = 0; i < *map_n; i++) {
    ids[i] = map[i].id;
    texts[i] = map[i].text;
  }
  prompt = workflow_expand_template(st->prompt, *prev_io, ids, texts, *map_n);
  if (!prompt) {
    fprintf(stderr, "neo: workflow:%s step:%s: template expand failed\n", wf_name, st->id);
    return -1;
  }
  if (st->tools_on) {
    char *sys = strdup("You are Neo workflow step. Follow the user instruction. Use tools if helpful.");
    if (!sys) {
      free(prompt);
      return -1;
    }
    memset(&resp, 0, sizeof(resp));
    if (agent_run_with_tools(conf, sys, NULL, 0, prompt, &resp) != 0) {
      fprintf(stderr, "neo: workflow:%s step:%s: llm+tools failed\n", wf_name, st->id);
      free(sys);
      free(prompt);
      llm_response_free(&resp);
      return -1;
    }
    free(sys);
  } else {
    memset(&resp, 0, sizeof(resp));
    if (llm_chat(conf->model.base_url, conf->model.name, conf->model.api_key,
                 conf->model.max_tokens, conf->model.temperature,
                 "You are Neo workflow step. Follow the user instruction.", prompt, &resp) != 0) {
      fprintf(stderr, "neo: workflow:%s step:%s: llm failed\n", wf_name, st->id);
      free(prompt);
      llm_response_free(&resp);
      return -1;
    }
  }
  free(prompt);
  wf_map_set(map, map_n, st->id, resp.data ? resp.data : "");
  free(*prev_io);
  *prev_io = resp.data ? strdup(resp.data) : strdup("");
  llm_response_free(&resp);
  return 0;
}

static const workflow_step_t *wf_find_step(const workflow_t *wf, const char *id) {
  int i;
  for (i = 0; i < wf->step_count; i++) {
    if (wf->steps[i].id && strcmp(wf->steps[i].id, id) == 0) return &wf->steps[i];
  }
  return NULL;
}

static int wf_run_step(const agent_config_t *conf, const char *root_real, const char *wf_name,
                       const workflow_t *wf, const workflow_step_t *st,
                       wf_out_map_t *map, int *map_n, char **prev_io) {
  int i, iter;
  if (st->type == WF_STEP_TOOL)
    return wf_run_tool_step(conf, root_real, wf_name, st, map, map_n, prev_io);
  if (st->type == WF_STEP_LLM)
    return wf_run_llm_step(conf, wf_name, st, map, map_n, prev_io);
  if (st->type == WF_STEP_LOOP) {
    for (iter = 0; iter < st->max_iters; iter++) {
      for (i = 0; i < st->over_count; i++) {
        const workflow_step_t *inner = wf_find_step(wf, st->over_ids[i]);
        if (!inner) {
          fprintf(stderr, "neo: workflow:%s step:%s: missing over id\n", wf_name, st->id);
          return -1;
        }
        if (wf_run_step(conf, root_real, wf_name, wf, inner, map, map_n, prev_io) != 0)
          return -1;
      }
    }
    return 0;
  }
  fprintf(stderr, "neo: workflow:%s step:%s: unknown type\n", wf_name, st->id);
  return -1;
}

int workflow_run(const agent_config_t *conf, const char *workflow_name, char **out_text) {
  const workflow_t *wf;
  char root_real[PATH_MAX];
  const char *root;
  wf_out_map_t map[WF_MAX_STEPS];
  int map_n = 0;
  char *prev = NULL;
  int i;

  if (out_text) *out_text = NULL;
  if (!conf || !workflow_name || !out_text) return -1;
  wf = config_find_workflow(conf, workflow_name);
  if (!wf) {
    fprintf(stderr, "neo: unknown workflow '%s'\n", workflow_name);
    if (conf->workflow_count > 0) {
      fprintf(stderr, "neo: available:");
      for (i = 0; i < conf->workflow_count; i++)
        fprintf(stderr, " %s", conf->workflows[i].name ? conf->workflows[i].name : "?");
      fprintf(stderr, "\n");
    }
    return -1;
  }

#if !defined(__APPLE__) && !defined(__linux__)
  fprintf(stderr, "neo: workflow not supported on this platform\n");
  return -1;
#else
  root = (conf->tools.root && conf->tools.root[0]) ? conf->tools.root : ".";
  if (!realpath(root, root_real)) {
    fprintf(stderr, "neo: tools.root realpath failed\n");
    return -1;
  }
  memset(map, 0, sizeof(map));

  /* Steps referenced by a loop's over are definition-only until the loop runs them. */
  for (i = 0; i < wf->step_count; i++) {
    int skip = 0;
    int j, k;
    if (wf->steps[i].type == WF_STEP_LOOP) {
      if (wf_run_step(conf, root_real, wf->name, wf, &wf->steps[i], map, &map_n, &prev) != 0) {
        wf_map_free(map, map_n);
        free(prev);
        return -1;
      }
      continue;
    }
    for (j = 0; j < wf->step_count; j++) {
      if (wf->steps[j].type != WF_STEP_LOOP) continue;
      for (k = 0; k < wf->steps[j].over_count; k++) {
        if (wf->steps[j].over_ids[k] && wf->steps[i].id &&
            strcmp(wf->steps[j].over_ids[k], wf->steps[i].id) == 0)
          skip = 1;
      }
    }
    if (skip) continue;
    if (wf_run_step(conf, root_real, wf->name, wf, &wf->steps[i], map, &map_n, &prev) != 0) {
      wf_map_free(map, map_n);
      free(prev);
      return -1;
    }
  }

  *out_text = prev ? prev : strdup("");
  wf_map_free(map, map_n);
  return 0;
#endif
}

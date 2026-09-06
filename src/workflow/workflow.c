/*
 * 确定性 workflow / DAG runner：tool、llm、loop、route。
 * 有 depends_on 或 route 时走拓扑排序；route 未选中分支 skip，汇合步按「依赖是否全 skip」决定。
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

/* 拼步骤类型摘要，供 -v / 失败路径 stderr 可读（如 type=tool tool=date_iso）。 */
static void wf_step_kind(const workflow_step_t *st, char *buf, size_t buf_sz) {
  if (!buf || buf_sz < 8) return;
  buf[0] = '\0';
  if (!st) {
    snprintf(buf, buf_sz, "type=?");
    return;
  }
  switch (st->type) {
    case WF_STEP_TOOL:
      snprintf(buf, buf_sz, "type=tool tool=%s", st->tool && st->tool[0] ? st->tool : "?");
      break;
    case WF_STEP_LLM:
      snprintf(buf, buf_sz, "type=llm tools=%s", st->tools_on ? "on" : "off");
      break;
    case WF_STEP_LOOP:
      snprintf(buf, buf_sz, "type=loop");
      break;
    case WF_STEP_ROUTE:
      snprintf(buf, buf_sz, "type=route");
      break;
    default:
      snprintf(buf, buf_sz, "type=?");
      break;
  }
}

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
        rep = ""; /* skipped / not-yet-run steps expand to empty */
        for (i = 0; i < n_maps; i++) {
          if (ids[i] && strcmp(ids[i], sid) == 0) {
            rep = texts[i] ? texts[i] : "";
            break;
          }
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
  const char *args_tmpl = (st->args_json && st->args_json[0]) ? st->args_json : "{}";
  char *args = NULL;
  const char *ids[WF_MAX_STEPS];
  const char *texts[WF_MAX_STEPS];
  int i;
  int attempts = 1 + (st->retry_max > 0 ? st->retry_max : 0);
  int attempt;
  int last_fail = -1;

  for (i = 0; i < *map_n; i++) {
    ids[i] = map[i].id;
    texts[i] = map[i].text;
  }
  /* tool args 也支持 {{steps.id}} / {{prev}}，便于把上一步输出写入 path/content 等字段。 */
  args = workflow_expand_template(args_tmpl, *prev_io, ids, texts, *map_n);
  if (!args) {
    fprintf(stderr, "neo: workflow:%s step:%s: args template expand failed\n", wf_name, st->id);
    return -1;
  }
  if (!conf->tools.enabled) {
    fprintf(stderr, "neo: workflow:%s step:%s: tools disabled\n", wf_name, st->id);
    free(args);
    return -1;
  }

  for (attempt = 0; attempt < attempts; attempt++) {
    free(out);
    out = NULL;
    out_len = 0;
    if (attempt > 0)
      fprintf(stderr, "neo: workflow:%s step:%s: retry attempt %d/%d\n", wf_name,
              st->id ? st->id : "?", attempt + 1, attempts);
    if (neo_dispatch_tool(conf, root_real, st->tool, args, &out, &out_len) != 0) {
      fprintf(stderr, "neo: workflow:%s step:%s: tool dispatch failed\n", wf_name, st->id);
      last_fail = -1;
      continue;
    }
    if (out && strncmp(out, "ERROR:", 6) == 0) {
      fprintf(stderr, "neo: workflow:%s step:%s: %s\n", wf_name, st->id, out);
      last_fail = -1;
      continue;
    }
    if (out && strncmp(out, "EXIT:", 5) == 0) {
      fprintf(stderr, "neo: workflow:%s step:%s: tool non-zero exit\n", wf_name, st->id);
      last_fail = -1;
      continue;
    }
    free(args);
    wf_map_set(map, map_n, st->id, out ? out : "");
    free(*prev_io);
    *prev_io = out ? strdup(out) : strdup("");
    free(out);
    return 0;
  }
  free(args);
  free(out);
  return last_fail;
}

/* 多路 cases：返回选中下标；无 cases 返回 -1；未命中且无默认返回 -1。 */
static int wf_route_select_case(const workflow_step_t *st, const char *expanded) {
  int i, def = -1;
  if (!st || st->route_case_count < 1) return -1;
  for (i = 0; i < st->route_case_count; i++) {
    const char *m = st->route_cases[i].match;
    if (!m || !m[0]) {
      def = i;
      continue;
    }
    if (expanded && strstr(expanded, m)) return i;
  }
  return def;
}

/* Max byte length ending on a UTF-8 boundary. */
static size_t utf8_prefix_len(const char *s, size_t max) {
  size_t i, n;
  if (!s || max == 0) return 0;
  n = strlen(s);
  if (n <= max) return n;
  i = max;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}

static char *wf_append_prior_outputs(char *prompt, const wf_out_map_t *map, int map_n) {
  size_t cap, len, i;
  size_t prior_budget = 20000; /* keep prompts bounded for LLM providers */
  char *out;
  if (!prompt) return NULL;
  if (map_n <= 0) return prompt;
  len = strlen(prompt);
  cap = len + prior_budget + 256;
  if (cap > 120000) cap = 120000;
  out = realloc(prompt, cap);
  if (!out) return prompt;
  prompt = out;
  len = strlen(prompt);
  if (len + 80 < cap) {
    snprintf(prompt + len, cap - len,
             "\n\n---\nPrior step outputs (already available — do NOT ask the user to paste them):\n");
    len = strlen(prompt);
  }
  for (i = 0; i < (size_t)map_n; i++) {
    size_t idl, tl, room, take, max_take;
    if (!map[i].id || !map[i].text || !map[i].text[0]) continue;
    if (strcmp(map[i].text, "then") == 0 || strcmp(map[i].text, "else") == 0) continue;
    idl = strlen(map[i].id);
    tl = strlen(map[i].text);
    /* Skip if template expand already inlined this output */
    if (tl >= 48) {
      char tip[64];
      size_t tip_n = utf8_prefix_len(map[i].text, 48);
      if (tip_n >= 24) {
        memcpy(tip, map[i].text, tip_n);
        tip[tip_n] = '\0';
        if (strstr(prompt, tip)) continue;
      }
    }
    room = (len + 1 < cap) ? (cap - len - 1) : 0;
    if (room < idl + 32) break;
    max_take = 6000;
    if (prior_budget < max_take) max_take = prior_budget;
    take = tl;
    if (take > max_take) take = max_take;
    if (idl + take + 16 > room) take = room > idl + 16 ? room - idl - 16 : 0;
    take = utf8_prefix_len(map[i].text, take);
    if (take == 0 && tl > 0) continue;
    snprintf(prompt + len, cap - len, "\n### %s\n", map[i].id);
    len = strlen(prompt);
    if (take > 0 && len + take < cap) {
      memcpy(prompt + len, map[i].text, take);
      len += take;
      prompt[len] = '\0';
      if (prior_budget > take) prior_budget -= take;
      else prior_budget = 0;
      if (take < tl && len + 24 < cap) {
        snprintf(prompt + len, cap - len, "\n...[truncated]...\n");
        len = strlen(prompt);
      } else if (len + 2 < cap) {
        prompt[len++] = '\n';
        prompt[len] = '\0';
      }
    }
    if (prior_budget < 64) break;
  }
  return prompt;
}

static int wf_run_llm_step(const agent_config_t *conf, const char *wf_name, const workflow_step_t *st,
                           wf_out_map_t *map, int *map_n, char **prev_io) {
  const char *ids[WF_MAX_STEPS];
  const char *texts[WF_MAX_STEPS];
  char *prompt = NULL;
  llm_response_t resp;
  const char *sys =
      "You are a Neo workflow worker step. Follow the user instruction. "
      "If prior step outputs are included below, treat them as given context and "
      "never ask the human to paste or re-send earlier drafts.";
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
  prompt = wf_append_prior_outputs(prompt, map, *map_n);
  if (st->tools_on) {
    char *sys_tools = strdup(sys);
    if (!sys_tools) {
      free(prompt);
      return -1;
    }
    memset(&resp, 0, sizeof(resp));
    if (agent_run_with_tools(conf, sys_tools, NULL, 0, prompt, &resp) != 0) {
      fprintf(stderr, "neo: workflow:%s step:%s: llm+tools failed\n", wf_name, st->id);
      free(sys_tools);
      free(prompt);
      llm_response_free(&resp);
      return -1;
    }
    free(sys_tools);
  } else {
    memset(&resp, 0, sizeof(resp));
    if (llm_chat(conf->model.base_url, conf->model.name, conf->model.api_key,
                 conf->model.max_tokens, conf->model.temperature, sys, prompt, &resp) != 0) {
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
  if (st->type == WF_STEP_ROUTE) {
    /* route handled by DAG runner; treat as no-op success if called directly */
    return 0;
  }
  fprintf(stderr, "neo: workflow:%s step:%s: unknown type\n", wf_name, st->id);
  return -1;
}

static int wf_index_of(const workflow_t *wf, const char *id) {
  int i;
  for (i = 0; i < wf->step_count; i++) {
    if (wf->steps[i].id && id && strcmp(wf->steps[i].id, id) == 0) return i;
  }
  return -1;
}

static int wf_is_dag_mode(const workflow_t *wf) {
  /* 任一 depends_on 或 route 步 → DAG 模式；否则保持线性 + loop。 */
  int i;
  for (i = 0; i < wf->step_count; i++) {
    if (wf->steps[i].depends_count > 0) return 1;
    if (wf->steps[i].type == WF_STEP_ROUTE) return 1;
  }
  return 0;
}

/* Kahn 拓扑排序；遇环返回 -1。order[] 为步骤下标。 */
static int wf_topo_sort(const workflow_t *wf, int *order, int *order_n) {
  int n = wf->step_count;
  int indeg[WF_MAX_STEPS];
  int i, j, left;
  if (n > WF_MAX_STEPS) return -1;
  memset(indeg, 0, sizeof(indeg));
  for (i = 0; i < n; i++) {
    for (j = 0; j < wf->steps[i].depends_count; j++) {
      if (wf_index_of(wf, wf->steps[i].depends_on[j]) < 0) return -1;
      indeg[i]++;
    }
  }
  *order_n = 0;
  left = n;
  while (left > 0) {
    int picked = -1;
    for (i = 0; i < n; i++) {
      if (indeg[i] == 0) {
        picked = i;
        break;
      }
    }
    if (picked < 0) {
      fprintf(stderr, "neo: workflow:%s: dependency cycle\n", wf->name);
      return -1;
    }
    order[(*order_n)++] = picked;
    indeg[picked] = -1;
    left--;
    for (i = 0; i < n; i++) {
      for (j = 0; j < wf->steps[i].depends_count; j++) {
        int d = wf_index_of(wf, wf->steps[i].depends_on[j]);
        if (d == picked && indeg[i] > 0) indeg[i]--;
      }
    }
  }
  return 0;
}

static void wf_mark_skip_ids(const workflow_t *wf, int *skip, char **ids, int nids) {
  int i, idx;
  for (i = 0; i < nids; i++) {
    idx = wf_index_of(wf, ids[i]);
    if (idx >= 0) skip[idx] = 1;
  }
}

static int wf_run_dag(const agent_config_t *conf, const char *root_real, const workflow_t *wf,
                      char **out_text, int verbose) {
  /* DAG 执行：拓扑序串行。route 命中则 skip 另一侧分支；
   * 汇合步仅当「全部 depends_on 都被 skip」时才 skip（一侧跑通仍执行）。 */
  int order[WF_MAX_STEPS];
  int order_n = 0;
  int skip[WF_MAX_STEPS];
  wf_out_map_t map[WF_MAX_STEPS];
  int map_n = 0;
  char *prev = NULL;
  int oi, i, j;

  memset(skip, 0, sizeof(skip));
  memset(map, 0, sizeof(map));
  if (wf_topo_sort(wf, order, &order_n) != 0) return -1;

  for (oi = 0; oi < order_n; oi++) {
    int idx = order[oi];
    const workflow_step_t *st = &wf->steps[idx];
    int dep_all_skipped = 1;
    int has_dep = 0;

    for (j = 0; j < st->depends_count; j++) {
      int d = wf_index_of(wf, st->depends_on[j]);
      if (d < 0) continue;
      has_dep = 1;
      if (!skip[d]) dep_all_skipped = 0;
    }
    /* 路由汇合：只有依赖全部被 skip 才 skip 本步；任一依赖跑过则继续。 */
    if (has_dep && dep_all_skipped) {
      char kind[96];
      skip[idx] = 1;
      wf_step_kind(st, kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=skip\n", wf->name,
                st->id ? st->id : "?", kind);
      continue;
    }
    if (skip[idx]) {
      char kind[96];
      wf_step_kind(st, kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=skip\n", wf->name,
                st->id ? st->id : "?", kind);
      continue;
    }

    if (st->type == WF_STEP_ROUTE) {
      const char *ids[WF_MAX_STEPS];
      const char *texts[WF_MAX_STEPS];
      char *expanded;
      char kind[96];
      int hit = 0;
      int sel = -1;
      for (i = 0; i < map_n; i++) {
        ids[i] = map[i].id;
        texts[i] = map[i].text;
      }
      expanded = workflow_expand_template(st->route_on, prev, ids, texts, map_n);
      if (!expanded) {
        fprintf(stderr, "neo: workflow:%s step:%s: route on expand failed\n", wf->name, st->id);
        wf_map_free(map, map_n);
        free(prev);
        return -1;
      }
      wf_step_kind(st, kind, sizeof(kind));
      if (st->route_case_count > 0) {
        /* 多路 cases：选中一支，skip 其余支的 then[] */
        sel = wf_route_select_case(st, expanded);
        for (i = 0; i < st->route_case_count; i++) {
          if (i == sel) continue;
          wf_mark_skip_ids(wf, skip, st->route_cases[i].then_ids, st->route_cases[i].then_count);
        }
        if (verbose)
          fprintf(stderr, "neo: step end   workflow=%s id=%s %s case=%d status=ok\n", wf->name,
                  st->id ? st->id : "?", kind, sel);
        {
          char label[32];
          if (sel >= 0)
            snprintf(label, sizeof(label), "case:%d", sel);
          else
            snprintf(label, sizeof(label), "case:none");
          wf_map_set(map, &map_n, st->id, label);
        }
      } else {
        if (st->route_match && st->route_match[0] && strstr(expanded, st->route_match))
          hit = 1;
        if (verbose)
          fprintf(stderr, "neo: step end   workflow=%s id=%s %s hit=%d status=ok\n", wf->name,
                  st->id ? st->id : "?", kind, hit);
        if (hit)
          wf_mark_skip_ids(wf, skip, st->route_else, st->route_else_count);
        else
          wf_mark_skip_ids(wf, skip, st->route_then, st->route_then_count);
        wf_map_set(map, &map_n, st->id, hit ? "then" : "else");
      }
      free(expanded);
      continue;
    }

    {
      char kind[96];
      wf_step_kind(st, kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step start workflow=%s id=%s %s\n", wf->name,
                st->id ? st->id : "?", kind);
      if (wf_run_step(conf, root_real, wf->name, wf, st, map, &map_n, &prev) != 0) {
        /* 失败摘要始终打出，便于无 -v 时定位卡在哪一步。 */
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=fail\n", wf->name,
                st->id ? st->id : "?", kind);
        wf_map_free(map, map_n);
        free(prev);
        return -1;
      }
      if (verbose)
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=ok\n", wf->name,
                st->id ? st->id : "?", kind);
    }
  }

  *out_text = prev ? prev : strdup("");
  wf_map_free(map, map_n);
  return 0;
}

int workflow_run(const agent_config_t *conf, const char *workflow_name, char **out_text,
                 int verbose) {
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

  if (wf_is_dag_mode(wf))
    {
      int r = wf_run_dag(conf, root_real, wf, out_text, verbose);
      if (r == 0)
        fprintf(stderr, "neo: run done workflow=%s status=ok\n", wf->name);
      return r;
    }

  memset(map, 0, sizeof(map));

  /* Legacy: steps referenced by a loop's over are definition-only until the loop runs them. */
  for (i = 0; i < wf->step_count; i++) {
    int skip = 0;
    int j, k;
    if (wf->steps[i].type == WF_STEP_LOOP) {
      char kind[96];
      wf_step_kind(&wf->steps[i], kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step start workflow=%s id=%s %s\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
      if (wf_run_step(conf, root_real, wf->name, wf, &wf->steps[i], map, &map_n, &prev) != 0) {
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=fail\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
        wf_map_free(map, map_n);
        free(prev);
        return -1;
      }
      if (verbose)
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=ok\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
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
    {
      char kind[96];
      wf_step_kind(&wf->steps[i], kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step start workflow=%s id=%s %s\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
      if (wf_run_step(conf, root_real, wf->name, wf, &wf->steps[i], map, &map_n, &prev) != 0) {
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=fail\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
        wf_map_free(map, map_n);
        free(prev);
        return -1;
      }
      if (verbose)
        fprintf(stderr, "neo: step end   workflow=%s id=%s %s status=ok\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
    }
  }

  *out_text = prev ? prev : strdup("");
  wf_map_free(map, map_n);
  fprintf(stderr, "neo: run done workflow=%s status=ok\n", wf->name);
  return 0;
#endif
}

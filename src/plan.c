/*
 * LLM plans a deterministic DAG (workflows YAML); runner executes it.
 */
#include "plan.h"
#include "llm.h"
#include "workflow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int plan_extract_workflows_yaml(const char *llm_text, char **out_yaml) {
  const char *p, *start = NULL, *end = NULL;
  size_t len;
  char *buf;
  if (out_yaml) *out_yaml = NULL;
  if (!llm_text || !out_yaml) return -1;

  /* Prefer ```yaml ... ``` or ``` ... ``` containing workflows: */
  p = strstr(llm_text, "```yaml");
  if (!p) p = strstr(llm_text, "```yml");
  if (!p) p = strstr(llm_text, "```");
  if (p) {
    p = strchr(p + 3, '\n');
    if (p) {
      start = p + 1;
      end = strstr(start, "```");
    }
  }
  if (!start || !end) {
    start = strstr(llm_text, "workflows:");
    if (!start) return -1;
    end = llm_text + strlen(llm_text);
  }
  while (start < end && (*start == '\r' || *start == ' ')) start++;
  if (strncmp(start, "workflows:", 10) != 0) {
    const char *w = strstr(start, "workflows:");
    if (!w || w >= end) return -1;
    start = w;
  }
  len = (size_t)(end - start);
  while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r' || start[len - 1] == ' ' ||
                     start[len - 1] == '`'))
    len--;
  buf = malloc(len + 2);
  if (!buf) return -1;
  memcpy(buf, start, len);
  buf[len] = '\n';
  buf[len + 1] = '\0';
  *out_yaml = buf;
  return 0;
}

int plan_resolve_target_steps(const agent_config_t *conf, int cli_steps) {
  int n = cli_steps;
  if (n <= 0 && conf && conf->plan.target_steps > 0)
    n = conf->plan.target_steps;
  if (n <= 0) n = PLAN_DEFAULT_TARGET_STEPS;
  if (n < 1) n = 1;
  if (n > PLAN_MAX_TARGET_STEPS) n = PLAN_MAX_TARGET_STEPS;
  return n;
}

char *plan_build_system_prompt(const agent_config_t *conf, int target_steps) {
  size_t cap = 12288;
  char *s = malloc(cap);
  size_t n = 0;
  int i, j;
  if (!s) return NULL;
  if (target_steps < 1) target_steps = PLAN_DEFAULT_TARGET_STEPS;
  if (target_steps > PLAN_MAX_TARGET_STEPS) target_steps = PLAN_MAX_TARGET_STEPS;
  n += (size_t)snprintf(s + n, cap - n,
                        "You are Neo's DAG planner. Given a user task, output ONE deterministic "
                        "workflow DAG as YAML only (no prose outside a yaml fence).\n"
                        "Rules:\n"
                        "- Topology is fixed after you emit it; workers do not replan.\n"
                        "- Use steps with id, type, depends_on, and type-specific fields.\n"
                        "- Allowed types: tool, llm, loop, route.\n"
                        "- Every step MUST set type: explicitly (tool|llm|loop|route).\n"
                        "- Default shape: a small software R&D team pipeline scaled to about %d steps.\n"
                        "  Role map (merge/split roles if N differs, but keep the phase order):\n"
                        "  1) understand requirements (llm)\n"
                        "  2-3) decompose / design (llm; optional tool)\n"
                        "  4-7) implement (llm and/or tool)\n"
                        "  8-9) verify (llm/tool); on failure route/loop back to implement or decompose "
                        "at most 2 times (use match PASS/FAIL or similar; no unbounded goto)\n"
                        "  10) summarize deliverable (llm; this should be the final useful output)\n"
                        "- Even for Q&A, use a scaled-down team flow (e.g. requirements → draft → "
                        "self-check → summarize), not a single undifferentiated dump — unless "
                        "target steps is 1.\n"
                        "- tool: only use tool names from the allowlist below (or read_file, "
                        "write_file, list_dir). type: tool requires a tool: field.\n"
                        "- llm: set prompt on one line in quotes; tools: on or off (step field; "
                        "do not emit a top-level tools: section inside workflows).\n"
                        "- tool args: use a JSON object on one line, e.g. args: {\"msg\":\"hi\"} "
                        "(not a quoted string wrapping the JSON).\n"
                        "- route: on must be a template like \"{{steps.verify_id}}\"; match is a "
                        "substring (e.g. PASS); then/else MUST be YAML lists of step ids, e.g. "
                        "then: [\"summarize\"] and else: [\"fix_impl\"] (never a bare string).\n"
                        "- Route merge: put ONLY exclusive branch work in then/else. A shared "
                        "join/summarize step should depend_on BOTH the route and the fix branch "
                        "(e.g. depends_on: [route_check, fix_content]) and must NOT also be listed "
                        "only on one side if you need it after both paths — prefer: then: [] or "
                        "then: [summarize] with summarize depending solely on route for the PASS "
                        "shortcut; OR then: [] / else: [fix], summarize depends_on "
                        "[route_check, fix] so it still runs when fix is skipped (join semantics).\n"
                        "- Final summarize prompts should use \"{{prev}}\" and/or "
                        "\"{{steps.draft_*}}\" / \"{{steps.fix_*}}\" together so content is "
                        "available whether or not the fix branch ran.\n"
                        "- Aim for about %d steps (a few more or fewer is OK if the task needs it). "
                        "Use depends_on for ordering.\n"
                        "- Output format:\n```yaml\nworkflows:\n  - name: planned\n    steps:\n"
                        "      - id: ...\n```\n\n"
                        "Allowed command tools:\n",
                        target_steps, target_steps);
  if (conf) {
    for (i = 0; i < conf->tools.command_count; i++) {
      const tool_command_t *cmd = &conf->tools.commands[i];
      if (!cmd->name) continue;
      if (n + 256 > cap) {
        char *ns = realloc(s, cap * 2);
        if (!ns) {
          free(s);
          return NULL;
        }
        s = ns;
        cap *= 2;
      }
      n += (size_t)snprintf(s + n, cap - n, "- %s", cmd->name);
      if (cmd->description) n += (size_t)snprintf(s + n, cap - n, ": %s", cmd->description);
      n += (size_t)snprintf(s + n, cap - n, "\n  argv:");
      for (j = 0; j < cmd->argv_count; j++)
        n += (size_t)snprintf(s + n, cap - n, " %s", cmd->argv[j] ? cmd->argv[j] : "");
      n += (size_t)snprintf(s + n, cap - n, "\n");
    }
  }
  if (!conf || conf->tools.command_count == 0)
    n += (size_t)snprintf(s + n, cap - n, "(none configured — use llm steps and/or read_file)\n");
  n += (size_t)snprintf(s + n, cap - n,
                        "\nBuiltin file tools (if tools.enabled): read_file, write_file, list_dir.\n");
  (void)n;
  return s;
}

static int write_quoted(FILE *f, const char *s) {
  fputc('"', f);
  if (s) {
    for (; *s; s++) {
      if (*s == '"' || *s == '\\') fputc('\\', f);
      fputc(*s, f);
    }
  }
  fputc('"', f);
  return 0;
}

int plan_materialize_config(const agent_config_t *base, const char *workflows_yaml,
                            agent_config_t *out_conf, char *out_path, size_t out_path_sz) {
  char path[] = "/tmp/neo-plan-XXXXXX";
  int fd;
  FILE *f;
  int i, j;
  if (!base || !workflows_yaml || !out_conf) return -1;
  fd = mkstemp(path);
  if (fd < 0) return -1;
  f = fdopen(fd, "w");
  if (!f) {
    close(fd);
    unlink(path);
    return -1;
  }
  fprintf(f, "model:\n  base_url: ");
  write_quoted(f, base->model.base_url);
  fprintf(f, "\n  name: ");
  write_quoted(f, base->model.name);
  fprintf(f, "\n  api_key: ");
  write_quoted(f, base->model.api_key ? base->model.api_key : "");
  fprintf(f, "\n  max_tokens: %d\n  temperature: %.2f\n",
          base->model.max_tokens > 0 ? base->model.max_tokens : 4096, base->model.temperature);
  fprintf(f, "tools:\n  enabled: %s\n  root: ", base->tools.enabled ? "true" : "false");
  write_quoted(f, base->tools.root && base->tools.root[0] ? base->tools.root : ".");
  fprintf(f, "\n  max_rounds: %d\n", base->tools.max_rounds > 0 ? base->tools.max_rounds : 16);
  if (base->tools.command_count > 0) {
    fprintf(f, "  commands:\n");
    for (i = 0; i < base->tools.command_count; i++) {
      const tool_command_t *cmd = &base->tools.commands[i];
      fprintf(f, "    - name: ");
      write_quoted(f, cmd->name);
      fprintf(f, "\n      description: ");
      write_quoted(f, cmd->description ? cmd->description : "");
      fprintf(f, "\n      argv: [");
      for (j = 0; j < cmd->argv_count; j++) {
        if (j) fputc(',', f);
        write_quoted(f, cmd->argv[j]);
      }
      fprintf(f, "]\n      timeout_sec: %d\n      max_output_bytes: %d\n      pass_args: %s\n",
              cmd->timeout_sec > 0 ? cmd->timeout_sec : 30,
              cmd->max_output_bytes > 0 ? cmd->max_output_bytes : 65536,
              cmd->pass_args ? "env" : "stdin_json");
    }
  }
  fprintf(f, "memory:\n  path: ");
  write_quoted(f, base->memory.path ? base->memory.path : "MEMORY.md");
  fprintf(f, "\n  max_chars: %d\nsession:\n  max_turns: %d\n",
          base->memory.max_chars > 0 ? base->memory.max_chars : 4000,
          base->session_max_turns > 0 ? base->session_max_turns : 10);
  fputs(workflows_yaml, f);
  if (workflows_yaml[0] && workflows_yaml[strlen(workflows_yaml) - 1] != '\n') fputc('\n', f);
  fclose(f);

  config_init(out_conf);
  if (config_load_file(out_conf, path) != 0) {
    unlink(path);
    return -1;
  }
  if (out_conf->workflow_count < 1) {
    fprintf(stderr, "neo plan: no workflows in planned YAML\n");
    config_free(out_conf);
    unlink(path);
    return -1;
  }
  if (out_path && out_path_sz) {
    snprintf(out_path, out_path_sz, "%s", path);
  } else {
    unlink(path);
  }
  return 0;
}

int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_yaml,
             int target_steps, const char *save_path, int debug) {
  char *sys = NULL;
  char *user = NULL;
  llm_response_t resp;
  char *yaml = NULL;
  agent_config_t planned;
  char tmp_path[256];
  int rc = -1;
  const char *wf_name;
  int steps;

  if (!conf || !task || !task[0]) return -1;
  steps = plan_resolve_target_steps(conf, target_steps);
  sys = plan_build_system_prompt(conf, steps);
  if (!sys) return -1;
  user = malloc(strlen(task) + 128);
  if (!user) {
    free(sys);
    return -1;
  }
  snprintf(user, strlen(task) + 128, "Task:\n%s\n\nEmit the workflows YAML now.", task);
  if (debug) {
    fprintf(stderr, "neo %s: calling LLM to build DAG...\n", do_run ? "run" : "plan");
  }
  memset(&resp, 0, sizeof(resp));
  if (llm_chat(conf->model.base_url, conf->model.name, conf->model.api_key,
               conf->model.max_tokens > 0 ? conf->model.max_tokens : 4096,
               conf->model.temperature, sys, user, &resp) != 0) {
    fprintf(stderr, "neo %s: LLM request failed\n", do_run ? "run" : "plan");
    free(sys);
    free(user);
    llm_response_free(&resp);
    return -1;
  }
  free(sys);
  free(user);
  if (plan_extract_workflows_yaml(resp.data ? resp.data : "", &yaml) != 0) {
    fprintf(stderr, "neo %s: could not extract workflows YAML from model output\n",
            do_run ? "run" : "plan");
    if (debug && resp.data) fprintf(stderr, "--- raw ---\n%s\n", resp.data);
    llm_response_free(&resp);
    return -1;
  }
  llm_response_free(&resp);

  if (!quiet_yaml) {
    fputs(yaml, stdout);
    if (yaml[0] && yaml[strlen(yaml) - 1] != '\n') fputc('\n', stdout);
    fflush(stdout);
  }

  if (save_path && save_path[0]) {
    FILE *sf = fopen(save_path, "w");
    if (!sf) {
      fprintf(stderr, "neo %s: cannot write %s\n", do_run ? "run" : "plan", save_path);
      free(yaml);
      return -1;
    }
    fputs(yaml, sf);
    fclose(sf);
    fprintf(stderr, "neo %s: saved %s\n", do_run ? "run" : "plan", save_path);
  }

  tmp_path[0] = '\0';
  if (plan_materialize_config(conf, yaml, &planned, tmp_path, sizeof(tmp_path)) != 0) {
    fprintf(stderr, "neo %s: planned DAG failed validation/load\n", do_run ? "run" : "plan");
    fprintf(stderr, "--- planned YAML ---\n%s", yaml);
    if (yaml[0] && yaml[strlen(yaml) - 1] != '\n') fputc('\n', stderr);
    fprintf(stderr, "--- end ---\n");
    free(yaml);
    return -1;
  }
  fprintf(stderr, "neo %s: validated workflow '%s' (%d steps)\n", do_run ? "run" : "plan",
          planned.workflows[0].name ? planned.workflows[0].name : "?",
          planned.workflows[0].step_count);
  wf_name = planned.workflows[0].name;

  if (do_run) {
    char *out = NULL;
    fprintf(stderr, "neo run: executing DAG '%s'\n", wf_name);
    if (workflow_run(&planned, wf_name, &out) != 0) {
      fprintf(stderr, "neo run: workflow run failed\n");
      free(out);
      config_free(&planned);
      if (tmp_path[0]) unlink(tmp_path);
      free(yaml);
      return -1;
    }
    if (out) {
      fputs(out, stdout);
      if (out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
      free(out);
    }
  }

  config_free(&planned);
  if (tmp_path[0]) unlink(tmp_path);
  free(yaml);
  rc = 0;
  return rc;
}

/*
 * LLM plans a deterministic DAG (workflows JSON); runner executes it.
 */
#include "plan.h"
#include "llm.h"
#include "workflow.h"
#include "yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

static yyjson_mut_val *mut_str(yyjson_mut_doc *doc, const char *s) {
  return yyjson_mut_str(doc, s ? s : "");
}

int plan_extract_workflows_json(const char *llm_text, char **out_json) {
  const char *p, *start = NULL, *end = NULL;
  char *slice = NULL;
  size_t len;
  yyjson_doc *doc = NULL;
  yyjson_val *root, *wfs;
  char *written = NULL;
  size_t wlen = 0;

  if (out_json) *out_json = NULL;
  if (!llm_text || !out_json) return -1;

  p = strstr(llm_text, "```json");
  if (!p) p = strstr(llm_text, "```JSON");
  if (!p) p = strstr(llm_text, "```");
  if (p) {
    p = strchr(p + 3, '\n');
    if (p) {
      start = p + 1;
      end = strstr(start, "```");
    }
  }
  if (start && end && end > start) {
    len = (size_t)(end - start);
    slice = malloc(len + 1);
    if (!slice) return -1;
    memcpy(slice, start, len);
    slice[len] = '\0';
  } else {
    start = strchr(llm_text, '{');
    if (!start) return -1;
    slice = strdup(start);
    if (!slice) return -1;
  }

  doc = yyjson_read(slice, strlen(slice), YYJSON_READ_JSON5);
  free(slice);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  wfs = yyjson_obj_get(root, "workflows");
  if (!yyjson_is_arr(wfs)) {
    yyjson_doc_free(doc);
    return -1;
  }

  {
    yyjson_mut_doc *mdoc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *mroot, *mwfs;
    if (!mdoc) {
      yyjson_doc_free(doc);
      return -1;
    }
    mroot = yyjson_mut_obj(mdoc);
    yyjson_mut_doc_set_root(mdoc, mroot);
    mwfs = yyjson_val_mut_copy(mdoc, wfs);
    if (!mwfs || !yyjson_mut_obj_add_val(mdoc, mroot, "workflows", mwfs)) {
      yyjson_mut_doc_free(mdoc);
      yyjson_doc_free(doc);
      return -1;
    }
    written = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, &wlen);
    yyjson_mut_doc_free(mdoc);
  }
  yyjson_doc_free(doc);
  if (!written) return -1;
  *out_json = written;
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
  n += (size_t)snprintf(
      s + n, cap - n,
      "You are Neo's DAG planner. Given a user task, output ONE deterministic "
      "workflow DAG as JSON only (no prose outside a json fence).\n"
      "Rules:\n"
      "- Topology is fixed after you emit it; workers do not replan.\n"
      "- Use steps with id, type, depends_on, and type-specific fields.\n"
      "- Allowed types: tool, llm, loop, route.\n"
      "- Every step MUST set type explicitly (tool|llm|loop|route).\n"
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
      "write_file, list_dir). type tool requires a tool field.\n"
      "- llm: set prompt; tools on or off as a step field string "
      "(\"on\"|\"off\").\n"
      "- tool args: JSON object, e.g. \"args\": {\"msg\":\"hi\"}.\n"
      "- route: \"on\" template like \"{{steps.verify_id}}\"; match substring; "
      "then/else MUST be JSON arrays of step ids.\n"
      "- Aim for about %d steps. Use depends_on arrays for ordering.\n"
      "- Output format:\n```json\n{\"workflows\":[{\"name\":\"planned\",\"steps\":["
      "{\"id\":\"...\",\"type\":\"llm\",\"prompt\":\"...\"}]}]}\n```\n\n"
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

int plan_materialize_config(const agent_config_t *base, const char *workflows_json,
                            agent_config_t *out_conf, char *out_path, size_t out_path_sz) {
  char path[] = "/tmp/neo-plan-XXXXXX";
  int fd;
  yyjson_doc *wf_doc = NULL;
  yyjson_val *wf_root, *wfs;
  yyjson_mut_doc *mdoc = NULL;
  yyjson_mut_val *root, *model, *tools, *mem, *sess, *cmds, *mwfs;
  char *written = NULL;
  size_t wlen = 0;
  FILE *f;
  int i, j;

  if (!base || !workflows_json || !out_conf) return -1;
  fd = mkstemp(path);
  if (fd < 0) return -1;

  wf_doc = yyjson_read(workflows_json, strlen(workflows_json), YYJSON_READ_JSON5);
  if (!wf_doc) {
    close(fd);
    unlink(path);
    return -1;
  }
  wf_root = yyjson_doc_get_root(wf_doc);
  wfs = yyjson_is_obj(wf_root) ? yyjson_obj_get(wf_root, "workflows") : NULL;
  if (!yyjson_is_arr(wfs)) {
    yyjson_doc_free(wf_doc);
    close(fd);
    unlink(path);
    return -1;
  }

  mdoc = yyjson_mut_doc_new(NULL);
  if (!mdoc) {
    yyjson_doc_free(wf_doc);
    close(fd);
    unlink(path);
    return -1;
  }
  root = yyjson_mut_obj(mdoc);
  yyjson_mut_doc_set_root(mdoc, root);

  model = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "model", model);
  yyjson_mut_obj_add_val(mdoc, model, "base_url", mut_str(mdoc, base->model.base_url));
  yyjson_mut_obj_add_val(mdoc, model, "name", mut_str(mdoc, base->model.name));
  yyjson_mut_obj_add_val(mdoc, model, "api_key",
                        mut_str(mdoc, base->model.api_key ? base->model.api_key : ""));
  yyjson_mut_obj_add_int(mdoc, model, "max_tokens",
                         base->model.max_tokens > 0 ? base->model.max_tokens : 4096);
  yyjson_mut_obj_add_real(mdoc, model, "temperature", base->model.temperature);

  tools = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "tools", tools);
  yyjson_mut_obj_add_bool(mdoc, tools, "enabled", base->tools.enabled ? true : false);
  yyjson_mut_obj_add_val(mdoc, tools, "root",
                         mut_str(mdoc, base->tools.root && base->tools.root[0] ? base->tools.root : "."));
  yyjson_mut_obj_add_int(mdoc, tools, "max_rounds",
                         base->tools.max_rounds > 0 ? base->tools.max_rounds : 16);
  if (base->tools.command_count > 0) {
    cmds = yyjson_mut_arr(mdoc);
    yyjson_mut_obj_add_val(mdoc, tools, "commands", cmds);
    for (i = 0; i < base->tools.command_count; i++) {
      const tool_command_t *cmd = &base->tools.commands[i];
      yyjson_mut_val *co = yyjson_mut_obj(mdoc);
      yyjson_mut_val *argv = yyjson_mut_arr(mdoc);
      yyjson_mut_arr_append(cmds, co);
      yyjson_mut_obj_add_val(mdoc, co, "name", mut_str(mdoc, cmd->name));
      yyjson_mut_obj_add_val(mdoc, co, "description",
                             mut_str(mdoc, cmd->description ? cmd->description : ""));
      yyjson_mut_obj_add_val(mdoc, co, "argv", argv);
      for (j = 0; j < cmd->argv_count; j++)
        yyjson_mut_arr_append(argv, mut_str(mdoc, cmd->argv[j]));
      yyjson_mut_obj_add_int(mdoc, co, "timeout_sec", cmd->timeout_sec > 0 ? cmd->timeout_sec : 30);
      yyjson_mut_obj_add_int(mdoc, co, "max_output_bytes",
                             cmd->max_output_bytes > 0 ? cmd->max_output_bytes : 65536);
      yyjson_mut_obj_add_val(mdoc, co, "pass_args",
                             mut_str(mdoc, cmd->pass_args ? "env" : "stdin_json"));
    }
  }

  mem = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "memory", mem);
  yyjson_mut_obj_add_val(mdoc, mem, "path",
                         mut_str(mdoc, base->memory.path ? base->memory.path : "MEMORY.md"));
  yyjson_mut_obj_add_int(mdoc, mem, "max_chars",
                         base->memory.max_chars > 0 ? base->memory.max_chars : 4000);

  sess = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "session", sess);
  yyjson_mut_obj_add_int(mdoc, sess, "max_turns",
                         base->session_max_turns > 0 ? base->session_max_turns : 10);

  mwfs = yyjson_val_mut_copy(mdoc, wfs);
  yyjson_mut_obj_add_val(mdoc, root, "workflows", mwfs);

  written = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, &wlen);
  yyjson_mut_doc_free(mdoc);
  yyjson_doc_free(wf_doc);
  if (!written) {
    close(fd);
    unlink(path);
    return -1;
  }

  f = fdopen(fd, "w");
  if (!f) {
    free(written);
    close(fd);
    unlink(path);
    return -1;
  }
  fwrite(written, 1, wlen, f);
  fclose(f);
  free(written);

  config_init(out_conf);
  if (config_load_file(out_conf, path) != 0) {
    unlink(path);
    return -1;
  }
  if (out_conf->workflow_count < 1) {
    fprintf(stderr, "neo plan: no workflows in planned JSON\n");
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

int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_plan,
             int target_steps, const char *save_path, int debug, int verbose) {
  char *sys = NULL;
  char *user = NULL;
  llm_response_t resp;
  char *json = NULL;
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
  snprintf(user, strlen(task) + 128, "Task:\n%s\n\nEmit the workflows JSON now.", task);
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
  if (plan_extract_workflows_json(resp.data ? resp.data : "", &json) != 0) {
    fprintf(stderr, "neo %s: could not extract workflows JSON from model output\n",
            do_run ? "run" : "plan");
    if (debug && resp.data) fprintf(stderr, "--- raw ---\n%s\n", resp.data);
    llm_response_free(&resp);
    return -1;
  }
  llm_response_free(&resp);

  if (!quiet_plan) {
    fputs(json, stdout);
    if (json[0] && json[strlen(json) - 1] != '\n') fputc('\n', stdout);
    fflush(stdout);
  }

  if (save_path && save_path[0]) {
    FILE *sf = fopen(save_path, "w");
    if (!sf) {
      fprintf(stderr, "neo %s: cannot write %s\n", do_run ? "run" : "plan", save_path);
      free(json);
      return -1;
    }
    fputs(json, sf);
    fclose(sf);
    fprintf(stderr, "neo %s: saved %s\n", do_run ? "run" : "plan", save_path);
  }

  tmp_path[0] = '\0';
  if (plan_materialize_config(conf, json, &planned, tmp_path, sizeof(tmp_path)) != 0) {
    fprintf(stderr, "neo %s: planned DAG failed validation/load\n", do_run ? "run" : "plan");
    fprintf(stderr, "--- planned JSON ---\n%s", json);
    if (json[0] && json[strlen(json) - 1] != '\n') fputc('\n', stderr);
    fprintf(stderr, "--- end ---\n");
    free(json);
    return -1;
  }
  if (verbose || debug)
    fprintf(stderr, "neo: plan ok steps=%d\n", planned.workflows[0].step_count);
  fprintf(stderr, "neo %s: validated workflow '%s' (%d steps)\n", do_run ? "run" : "plan",
          planned.workflows[0].name ? planned.workflows[0].name : "?",
          planned.workflows[0].step_count);
  wf_name = planned.workflows[0].name;

  if (do_run) {
    char *out = NULL;
    fprintf(stderr, "neo run: executing DAG '%s'\n", wf_name);
    if (workflow_run(&planned, wf_name, &out, verbose) != 0) {
      fprintf(stderr, "neo run: workflow run failed\n");
      free(out);
      config_free(&planned);
      if (tmp_path[0]) unlink(tmp_path);
      free(json);
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
  free(json);
  rc = 0;
  return rc;
}

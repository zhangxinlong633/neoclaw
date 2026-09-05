#ifndef NEO_PLAN_H
#define NEO_PLAN_H

#include "config.h"
#include <stddef.h>

#define PLAN_DEFAULT_TARGET_STEPS 10
#define PLAN_MAX_TARGET_STEPS 32

/*
 * Resolve soft target step count: CLI > conf->plan.target_steps > default 10.
 * Clamped to 1..PLAN_MAX_TARGET_STEPS. cli_steps <= 0 means unset.
 */
int plan_resolve_target_steps(const agent_config_t *conf, int cli_steps);

/*
 * Extract a workflows: YAML document from planner LLM text.
 * Accepts fenced ```yaml blocks or a bare workflows: section.
 * Caller frees *out_yaml. Returns 0 on success.
 */
int plan_extract_workflows_yaml(const char *llm_text, char **out_yaml);

/*
 * Build planner system prompt listing allowed tools (builtins + commands).
 * target_steps is the soft size preference (already resolved).
 * Caller frees. Returns NULL on OOM.
 */
char *plan_build_system_prompt(const agent_config_t *conf, int target_steps);

/*
 * Write a temporary config that merges conf's model/tools with workflows_yaml,
 * load into out_conf. on success out_path may receive tmp path (caller unlink) if non-NULL.
 * Returns 0 on success.
 */
int plan_materialize_config(const agent_config_t *base, const char *workflows_yaml,
                            agent_config_t *out_conf, char *out_path, size_t out_path_sz);

/*
 * Ask LLM to plan task into a DAG; validate; optionally run.
 * target_steps: soft preference (use plan_resolve_target_steps before calling, or pass <=0 to resolve).
 * If quiet_yaml is 0, print planned YAML to stdout.
 * If do_run is set, execute and print workflow result to stdout.
 * Returns 0 on success.
 */
int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_yaml,
             int target_steps, const char *save_path, int debug);

#endif

#ifndef NEO_PLAN_H
#define NEO_PLAN_H

#include "config.h"
#include <stddef.h>

#define PLAN_DEFAULT_TARGET_STEPS 10
#define PLAN_MAX_TARGET_STEPS 32

/*
 * neo plan / neo run：让 LLM 一次性规划，再确定性执行。
 * 优先解析 {"use":[...]} 选用 workflow_directory 中的图；否则解析完整 workflows JSON 现编。
 */

/* 软步数目标：CLI > conf->plan.target_steps > 默认 10；夹紧到 1..PLAN_MAX_TARGET_STEPS。 */
int plan_resolve_target_steps(const agent_config_t *conf, int cli_steps);

/* 从模型输出抽出含 "workflows" 的 JSON（支持 fenced 代码块）。调用方 free *out_json。 */
int plan_extract_workflows_json(const char *llm_text, char **out_json);

/* 抽出 {"use":"name"} 或 {"use":["a","b"]}。调用方 plan_free_use。 */
int plan_extract_use(const char *llm_text, char ***out_names, int *out_n);
void plan_free_use(char **names, int n);

/* 拼 planner system prompt：DAG catalog + 能力矩阵名单 + 知识/工程路由规则。调用方 free。 */
char *plan_build_system_prompt(const agent_config_t *conf, int target_steps);

/* 把规划出的 workflows 与 base 的 model/capability_matrix 合成临时配置并 load 校验。 */
int plan_materialize_config(const agent_config_t *base, const char *workflows_json,
                            agent_config_t *out_conf, char *out_path, size_t out_path_sz);

/*
 * 调 LLM → 优先 use 选型跑目录 DAG，否则 materialize 现编图并可选执行。
 * quiet_plan=0 时把 use/workflows JSON 打到 stdout。
 */
int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_plan,
             int target_steps, const char *save_path, int debug, int verbose);

#endif

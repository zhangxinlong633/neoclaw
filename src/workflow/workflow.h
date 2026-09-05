#ifndef NEO_WORKFLOW_H
#define NEO_WORKFLOW_H

#include "config.h"

/*
 * 确定性 DAG / workflow 执行器。
 * 拓扑在配置里冻结；LLM 只做 worker，不在执行期改图。tool 步绑定 Capability Matrix 的 name。
 */

/* 展开 {{prev}} / {{steps.<id>}}；未知变量返回 NULL。调用方 free。 */
char *workflow_expand_template(const char *tmpl, const char *prev,
                               const char **ids, const char **texts, int n_maps);

/* 按名运行 workflow；成功时 *out_text 为最终输出（调用方 free）。verbose：stderr 步骤日志。 */
int workflow_run(const agent_config_t *conf, const char *workflow_name, char **out_text,
                 int verbose);

#endif

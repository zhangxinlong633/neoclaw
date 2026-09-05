#ifndef NEO_CAPABILITY_DIR_H
#define NEO_CAPABILITY_DIR_H

#include "config.h"

/*
 * 能力目录加载器：把 capability_matrix.directory 下「一文件一能力」灌进 conf->tools.commands。
 * proposed/ 默认不加载；propose_capability 只写草稿、当轮不进矩阵（方案 A）。
 */

/* 按 manifest.load[] 扫描子目录 *.json5，追加到 conf->tools.commands。无 directory 则空操作。 */
int capability_dir_load_into_config(agent_config_t *conf);

/*
 * propose_capability 工具实现：校验 name/argv 后写入 directory/proposed/<name>.json5。
 * 不修改当前进程矩阵。调用方释放 *out_text；含 ERROR: 前缀的说明也返回 0（已处理）。
 */
int capability_dir_propose(const agent_config_t *conf, const char *args_json, char **out_text);

/* 从 manifest 解析 proposed 子目录名（默认 "proposed"）；堆分配，调用方 free。 */
char *capability_dir_proposed_subdir(const agent_config_t *conf);

#endif

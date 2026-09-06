#ifndef NEO_WORKFLOW_DIR_H
#define NEO_WORKFLOW_DIR_H

#include "config.h"

/*
 * DAG 目录加载器：dag_directory 下一图一文件，灌进 conf->dags。
 * 与能力目录对称；proposed/ 默认不加载。catalog 文本供 planner 优先 {"use":[...]} 选型。
 */

/* 按 manifest.load[] 加载 library 等子目录中的 workflow 对象。 */
int dag_dir_load_into_config(agent_config_t *conf);

/* 生成 planner 用的 DAG catalog（name/description/when/when_not/requires/outcome/tags）；调用方 free。 */
char *dag_dir_catalog_listing(const agent_config_t *conf);

#endif

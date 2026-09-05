#ifndef NEO_WORKFLOW_H
#define NEO_WORKFLOW_H

#include "config.h"

/* Expand {{prev}} and {{steps.<id>}}. Returns malloc'd string or NULL on unknown var. */
char *workflow_expand_template(const char *tmpl, const char *prev,
                               const char **ids, const char **texts, int n_maps);

/* Run named workflow; caller frees *out_text. Returns 0 on success.
 * verbose: structured step lines on stderr. */
int workflow_run(const agent_config_t *conf, const char *workflow_name, char **out_text,
                 int verbose);

#endif

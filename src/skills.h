#ifndef NEO_SKILLS_H
#define NEO_SKILLS_H

#include "config.h"
#include <stddef.h>

/* Append skills to system prompt. priority_filter: 1=only high-priority, 0=only normal, -1=all. High-priority skills should be appended first (right after time) for short-context models. */
void skills_append_to_system_prompt(agent_config_t *conf, const char *user_message, char *dest, size_t cap, int priority_filter);

#endif

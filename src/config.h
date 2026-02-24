#ifndef NEO_CONFIG_H
#define NEO_CONFIG_H

typedef struct {
  char *provider;
  char *base_url;
  char *name;
  char *api_key;
  int max_tokens;
  double temperature;
} model_config_t;

typedef struct {
  char **paths;
  int *priority;   /* 0=normal, 1=high; high is always injected full */
  int path_count;
  int unmatched;   /* 0=inject index when not matched (default), 1=skip to save context */
  char *directory; /* optional: scan dir for subdir/SKILL.md and add as paths */
  char **high_priority; /* names or paths to mark as high priority (e.g. ["nanjing"]) */
  int high_priority_count;
} skills_config_t;

typedef struct {
  char **paths;
  int path_count;
  int max_chars_per_file;
} bootstrap_config_t;

typedef struct {
  char *path;
  int max_chars;
} memory_config_t;

typedef struct {
  model_config_t model;
  bootstrap_config_t bootstrap;
  skills_config_t skills;
  memory_config_t memory;
  int session_max_turns;
} agent_config_t;

void config_init(agent_config_t *c);
void config_free(agent_config_t *c);
int config_load_file(agent_config_t *c, const char *path);
void config_apply_env(agent_config_t *c);

#endif

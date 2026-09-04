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

/* OpenClaw-style persona file (injected before bootstrap as ## Soul) */
typedef struct {
  char *path;
  int max_chars;
} soul_config_t;

/* Project rules / constraints (like Cursor rules; injected after bootstrap) */
typedef struct {
  char **paths;
  int path_count;
  int max_chars_per_file;
} rules_config_t;

/* Anchor prompts to the process working directory (run neo from repo root). */
typedef struct {
  int prompt_cwd; /* 1: append cwd to system prompt */
} workspace_config_t;

typedef struct {
  int enabled;       /* 0 off, 1 on (read_file / write_file under root) */
  char *root;        /* sandbox root directory (default ".") */
  int max_rounds;    /* max tool API rounds (default 16) */
  int max_read_bytes; /* per read_file (default 262144) */
  int list_dir_max_entries; /* max names returned by list_dir (default 256) */
  int http_fetch_enabled;   /* 0 off: do not register http_get */
  char *http_allow_hosts;   /* comma-separated hostnames, e.g. "api.github.com,httpbin.org" */
  int http_fetch_max_bytes; /* cap response body (default 262144) */
} tools_config_t;

typedef struct {
  model_config_t model;
  bootstrap_config_t bootstrap;
  soul_config_t soul;
  rules_config_t rules;
  workspace_config_t workspace;
  skills_config_t skills;
  memory_config_t memory;
  tools_config_t tools;
  int session_max_turns;
} agent_config_t;

void config_init(agent_config_t *c);
void config_free(agent_config_t *c);
int config_load_file(agent_config_t *c, const char *path);
void config_apply_env(agent_config_t *c);

#endif

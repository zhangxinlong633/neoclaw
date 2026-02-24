/*
 * Neo: minimal C agent. One process per query, or daemon mode.
 * Usage: neo [OPTIONS] "user message"
 *        neo daemon [--socket PATH]
 * Env:   NEO_CONFIG, NEO_MODEL, NEO_API_KEY
 * Output: LLM response to stdout.
 */
#include "config.h"
#include "daemon.h"
#include "llm.h"
#include "skills.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYSTEM_MAX (256 * 1024)
#define USER_MAX   (64 * 1024)

static size_t read_file_into(char *buf, size_t cap, const char *path, size_t max_chars) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  size_t n = 0;
  if (max_chars <= 0 || max_chars > cap - 1) max_chars = cap - 1;
  while (n < max_chars && fgets(buf + n, (int)(cap - n), f))
    n = strlen(buf);
  if (n >= cap - 1) n = cap - 2;
  buf[n] = '\0';
  fclose(f);
  return n;
}

static void append_section(char *dest, size_t cap, const char *title, const char *path, const char *content) {
  if (!content || !content[0]) return;
  size_t used = strlen(dest);
  if (used + strlen(title) + strlen(path) + strlen(content) + 64 > cap) return;
  strncat(dest, title, cap - used - 1);
  strncat(dest, path, cap - used - 1);
  strncat(dest, "\n\n", cap - used - 1);
  strncat(dest, content, cap - used - 1);
  strncat(dest, "\n\n", cap - used - 1);
}

static void build_user_message(char *buf, size_t cap, char **argv, int start, int argc) {
  buf[0] = '\0';
  for (int i = start; i < argc; i++) {
    if (i > start) strncat(buf, " ", cap - strlen(buf) - 1);
    strncat(buf, argv[i], cap - strlen(buf) - 1);
  }
}

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s [OPTIONS] \"your message\"\n", prog);
  fprintf(stderr, "       %s daemon [--socket PATH]\n", prog);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -c, --config PATH   Config file (default: config.yaml or NEO_CONFIG)\n");
  fprintf(stderr, "  -m, --model NAME    Override model name\n");
  fprintf(stderr, "  -d, --debug         Print system prompt, user message and request params to stderr\n");
  fprintf(stderr, "  -h, --help          Show this help\n");
  fprintf(stderr, "  daemon              Run as daemon: read from stdin, reply to stdout\n");
  fprintf(stderr, "  --socket PATH       (with daemon) Listen on Unix socket instead of stdin\n");
}

/* ANSI colors for debug (no-op if stderr not a tty; call debug_color_ok() to decide) */
static int debug_color_ok(void) {
  const char *t = getenv("TERM");
  return t && t[0] && strcmp(t, "dumb") != 0;
}
#define D_RESET   "\033[0m"
#define D_CYAN    "\033[36m"
#define D_YELLOW  "\033[33m"
#define D_GREEN   "\033[32m"
#define D_BOLD    "\033[1m"

static void debug_print_request(agent_config_t *conf,
                                const char *base_url, const char *model, int max_tokens, double temperature,
                                const char *system_prompt, const char *user_message) {
  int use_color = debug_color_ok();
  const char *cy = use_color ? D_CYAN : "";
  const char *yl = use_color ? D_YELLOW : "";
  const char *gr = use_color ? D_GREEN : "";
  const char *bd = use_color ? D_BOLD : "";
  const char *re = use_color ? D_RESET : "";

  fprintf(stderr, "\n%s%s=== NEO DEBUG: request params ===%s\n", bd, cy, re);
  fprintf(stderr, "%sbase_url: %s\nmodel: %s\nmax_tokens: %d\ntemperature: %.2f\n%s",
          cy, base_url ? base_url : "(null)", model ? model : "(null)", max_tokens, temperature, re);
  if (conf && conf->skills.path_count > 0) {
    fprintf(stderr, "%sloaded skills: ", cy);
    for (int i = 0; i < conf->skills.path_count; i++)
      fprintf(stderr, "%s%s", i ? ", " : "", conf->skills.paths[i] ? conf->skills.paths[i] : "(null)");
    fprintf(stderr, "%s\n", re);
    if (conf->skills.unmatched)
      fprintf(stderr, "%s(unmatched: skip → only high-priority + matched skills in prompt; set unmatched: index to include short index for all)%s\n", cy, re);
  }
  fprintf(stderr, "\n%s%s=== NEO DEBUG: system prompt (%zu chars) ===%s\n%s%s%s\n%s%s=== END system prompt ===%s\n",
          bd, yl, system_prompt ? strlen(system_prompt) : 0u, re, yl, system_prompt ? system_prompt : "", re, bd, yl, re);
  fprintf(stderr, "\n%s%s=== NEO DEBUG: user message (%zu chars) ===%s\n%s%s%s\n%s%s=== END user message ===%s\n\n",
          bd, gr, user_message ? strlen(user_message) : 0u, re, gr, user_message ? user_message : "", re, bd, gr, re);
}

int main(int argc, char **argv) {
  const char *config_path = getenv("NEO_CONFIG");
  if (!config_path || !config_path[0]) config_path = "config.yaml";
  const char *model_override = NULL;
  int arg_start = 1;

  const char *socket_path = NULL;
  int daemon_mode = 0;
  int debug = 0;

  while (arg_start < argc) {
    if (strcmp(argv[arg_start], "--help") == 0 || strcmp(argv[arg_start], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[arg_start], "--config") == 0 || strcmp(argv[arg_start], "-c") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --config requires PATH\n"); return 1; }
      config_path = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--model") == 0 || strcmp(argv[arg_start], "-m") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --model requires NAME\n"); return 1; }
      model_override = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "daemon") == 0) {
      daemon_mode = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--socket") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --socket requires PATH\n"); return 1; }
      socket_path = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--debug") == 0 || strcmp(argv[arg_start], "-d") == 0) {
      debug = 1;
      arg_start++;
      continue;
    }
    break;
  }

  if (daemon_mode) {
    agent_config_t conf;
    config_init(&conf);
    if (config_load_file(&conf, config_path) != 0) {
      fprintf(stderr, "neo: failed to load config from %s\n", config_path);
      config_free(&conf);
      return 1;
    }
    config_apply_env(&conf);
    if (model_override) {
      free(conf.model.name);
      conf.model.name = malloc(strlen(model_override) + 1);
      if (conf.model.name) strcpy(conf.model.name, model_override);
    }
    int r = socket_path ? run_daemon_socket(&conf, socket_path, debug) : run_daemon_stdin(&conf, debug);
    config_free(&conf);
    return r != 0;
  }

  if (arg_start >= argc) {
    fprintf(stderr, "Usage: neo [OPTIONS] \"your message\" or neo daemon [--socket PATH]\n");
    return 1;
  }

  agent_config_t conf;
  config_init(&conf);
  if (config_load_file(&conf, config_path) != 0) {
    fprintf(stderr, "neo: failed to load config from %s\n", config_path);
    config_free(&conf);
    return 1;
  }
  config_apply_env(&conf);
  if (model_override) {
    free(conf.model.name);
    conf.model.name = malloc(strlen(model_override) + 1);
    if (conf.model.name) strcpy(conf.model.name, model_override);
  }

  char *system_prompt = malloc(SYSTEM_MAX);
  char *user_message  = malloc(USER_MAX);
  char *tmp           = malloc(65536);
  if (!system_prompt || !user_message) {
    free(system_prompt);
    free(user_message);
    free(tmp);
    config_free(&conf);
    return 1;
  }
  system_prompt[0] = '\0';
  user_message[0]  = '\0';
  if (!tmp) tmp = malloc(1024);

  strncat(system_prompt, "You are a helpful assistant. Follow any skill and bootstrap instructions below.\n\n", SYSTEM_MAX - 1);

  {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char datebuf[80];
    char line[96];
    if (utc && strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M UTC", utc) > 0)
      snprintf(line, sizeof(line), "Current date and time: %s\n\n", datebuf);
    else
      strcpy(line, "Current date and time: (unknown)\n\n");
    strncat(system_prompt, line, SYSTEM_MAX - 1);
  }

  build_user_message(user_message, USER_MAX, argv, arg_start, argc);
  skills_append_to_system_prompt(&conf, user_message, system_prompt, SYSTEM_MAX, 1); /* high priority first */
  if (tmp) {
    for (int i = 0; i < conf.bootstrap.path_count; i++) {
      const char *path = conf.bootstrap.paths[i];
      size_t max_c = (conf.bootstrap.max_chars_per_file > 0) ? (size_t)conf.bootstrap.max_chars_per_file : 8000;
      if (read_file_into(tmp, 65536, path, max_c) > 0)
        append_section(system_prompt, SYSTEM_MAX, "## Bootstrap: ", path, tmp);
    }
  }
  skills_append_to_system_prompt(&conf, user_message, system_prompt, SYSTEM_MAX, 0); /* normal skills */

  if (conf.memory.path && tmp) {
    if (read_file_into(tmp, 65536, conf.memory.path, (size_t)conf.memory.max_chars) > 0)
      append_section(system_prompt, SYSTEM_MAX, "## Memory (context)\n\n", "", tmp);
  }

  if (debug)
    debug_print_request(&conf, conf.model.base_url, conf.model.name, conf.model.max_tokens, conf.model.temperature,
                       system_prompt, user_message);

  llm_response_t resp = {0};
  int err = llm_chat(
    conf.model.base_url,
    conf.model.name,
    conf.model.api_key,
    conf.model.max_tokens,
    conf.model.temperature,
    system_prompt,
    user_message,
    &resp
  );
  config_free(&conf);
  free(system_prompt);
  free(user_message);
  free(tmp);

  if (err != 0) {
    fprintf(stderr, "neo: LLM request failed\n");
    llm_response_free(&resp);
    return 1;
  }
  if (resp.data && resp.size) {
    fwrite(resp.data, 1, resp.size, stdout);
    if (resp.data[resp.size - 1] != '\n') putchar('\n');
  }
  llm_response_free(&resp);
  return 0;
}

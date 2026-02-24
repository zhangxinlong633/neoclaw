/*
 * Daemon mode: stdin loop or Unix socket server, with session history.
 */
#include "config.h"
#include "llm.h"
#include "skills.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYSTEM_MAX (256 * 1024)
#define LINE_MAX   (64 * 1024)

#if defined(__linux__) || defined(__APPLE__)
#define HAVE_UNIX_SOCKET 1
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#endif

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

static void build_system_prompt(agent_config_t *conf, const char *user_message, char *out, size_t cap) {
  char *tmp = malloc(65536);
  if (!tmp) { out[0] = '\0'; return; }
  out[0] = '\0';
  strncat(out, "You are a helpful assistant. Follow any skill and bootstrap instructions below.\n\n", cap - 1);
  {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char datebuf[80];
    char line[96];
    if (utc && strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M UTC", utc) > 0)
      snprintf(line, sizeof(line), "Current date and time: %s\n\n", datebuf);
    else
      strcpy(line, "Current date and time: (unknown)\n\n");
    strncat(out, line, cap - 1);
  }
  skills_append_to_system_prompt(conf, user_message, out, cap, 1); /* high priority first */
  for (int i = 0; i < conf->bootstrap.path_count; i++) {
    size_t max_c = (conf->bootstrap.max_chars_per_file > 0) ? (size_t)conf->bootstrap.max_chars_per_file : 8000;
    if (read_file_into(tmp, 65536, conf->bootstrap.paths[i], max_c) > 0)
      append_section(out, cap, "## Bootstrap: ", conf->bootstrap.paths[i], tmp);
  }
  skills_append_to_system_prompt(conf, user_message, out, cap, 0); /* normal skills */
  if (conf->memory.path) {
    if (read_file_into(tmp, 65536, conf->memory.path, (size_t)conf->memory.max_chars) > 0)
      append_section(out, cap, "## Memory (context)\n\n", "", tmp);
  }
  free(tmp);
}

#define MAX_SESSION_MESSAGES 64
static struct {
  char *role;
  char *content;
} session_messages[MAX_SESSION_MESSAGES];
static int session_count = 0;

static void session_append(const char *role, const char *content) {
  if (!role || !content) return;
  if (session_count >= MAX_SESSION_MESSAGES) {
    free(session_messages[0].role);
    free(session_messages[0].content);
    memmove(&session_messages[0], &session_messages[1], (session_count - 1) * sizeof(session_messages[0]));
    session_count--;
  }
  session_messages[session_count].role = strdup(role);
  session_messages[session_count].content = strdup(content);
  if (session_messages[session_count].role && session_messages[session_count].content)
    session_count++;
  else {
    free(session_messages[session_count].role);
    free(session_messages[session_count].content);
  }
}

static void session_trim_to(int max_turns) {
  int max_msg = max_turns * 2;
  while (session_count > max_msg) {
    free(session_messages[0].role);
    free(session_messages[0].content);
    memmove(&session_messages[0], &session_messages[1], (session_count - 1) * sizeof(session_messages[0]));
    session_count--;
  }
}

static int do_one_turn(agent_config_t *conf, char *system_prompt, const char *user_input, llm_response_t *out) {
  llm_message_t *msgs = malloc((session_count + 1) * sizeof(llm_message_t));
  if (!msgs) return -1;
  int n = 0;
  for (int i = 0; i < session_count && session_messages[i].content; i++)
    msgs[n++] = (llm_message_t){ session_messages[i].role, session_messages[i].content };
  msgs[n++] = (llm_message_t){ "user", user_input };
  int err = llm_chat_messages(
    conf->model.base_url, conf->model.name, conf->model.api_key,
    conf->model.max_tokens, conf->model.temperature,
    system_prompt, msgs, n, out);
  free(msgs);
  return err;
}

#define D_RESET   "\033[0m"
#define D_CYAN    "\033[36m"
#define D_YELLOW  "\033[33m"
#define D_GREEN   "\033[32m"
#define D_BOLD    "\033[1m"
static void daemon_debug_print(agent_config_t *conf, const char *system_prompt, const char *user_message) {
  const char *t = getenv("TERM");
  int use_color = t && t[0] && strcmp(t, "dumb") != 0;
  const char *cy = use_color ? D_CYAN : "";
  const char *yl = use_color ? D_YELLOW : "";
  const char *gr = use_color ? D_GREEN : "";
  const char *bd = use_color ? D_BOLD : "";
  const char *re = use_color ? D_RESET : "";
  fprintf(stderr, "\n%s%s=== NEO DEBUG: request params ===%s\n", bd, cy, re);
  fprintf(stderr, "%sbase_url: %s\nmodel: %s\nmax_tokens: %d\ntemperature: %.2f\n%s", cy,
          conf->model.base_url ? conf->model.base_url : "(null)", conf->model.name ? conf->model.name : "(null)",
          conf->model.max_tokens, conf->model.temperature, re);
  if (conf->skills.path_count > 0) {
    fprintf(stderr, "%sloaded skills: ", cy);
    for (int i = 0; i < conf->skills.path_count; i++)
      fprintf(stderr, "%s%s", i ? ", " : "", conf->skills.paths[i] ? conf->skills.paths[i] : "(null)");
    fprintf(stderr, "%s\n", re);
  }
  fprintf(stderr, "\n%s%s=== NEO DEBUG: system prompt (%zu chars) ===%s\n%s%s%s\n%s%s=== END system prompt ===%s\n",
          bd, yl, strlen(system_prompt), re, yl, system_prompt, re, bd, yl, re);
  fprintf(stderr, "\n%s%s=== NEO DEBUG: user message (%zu chars) ===%s\n%s%s%s\n%s%s=== END user message ===%s\n\n",
          bd, gr, strlen(user_message), re, gr, user_message, re, bd, gr, re);
}

int run_daemon_stdin(agent_config_t *conf, int debug) {
  char *system_prompt = malloc(SYSTEM_MAX);
  char *line_buf = malloc(LINE_MAX);
  if (!system_prompt || !line_buf) {
    free(system_prompt);
    free(line_buf);
    return -1;
  }
  session_count = 0;
  fprintf(stderr, "neo daemon: stdin mode. Type 'exit' or 'quit' or EOF to stop.\n");
  while (fgets(line_buf, LINE_MAX, stdin)) {
    size_t len = strlen(line_buf);
    while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) line_buf[--len] = '\0';
    if (len == 0) continue;
    if (strcmp(line_buf, "exit") == 0 || strcmp(line_buf, "quit") == 0) break;
    build_system_prompt(conf, line_buf, system_prompt, SYSTEM_MAX);
    if (debug) daemon_debug_print(conf, system_prompt, line_buf);
    llm_response_t resp = {0};
    if (do_one_turn(conf, system_prompt, line_buf, &resp) != 0) {
      fprintf(stderr, "neo: LLM request failed\n");
      llm_response_free(&resp);
      continue;
    }
    if (resp.data && resp.size) {
      fwrite(resp.data, 1, resp.size, stdout);
      if (resp.size > 0 && resp.data[resp.size - 1] != '\n') putchar('\n');
      fflush(stdout);
      session_append("user", line_buf);
      session_append("assistant", resp.data);
      session_trim_to(conf->session_max_turns > 0 ? conf->session_max_turns : 10);
    }
    llm_response_free(&resp);
  }
  free(system_prompt);
  free(line_buf);
  return 0;
}

#ifdef HAVE_UNIX_SOCKET
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
  unlink(socket_path);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 5) < 0) {
    perror("listen");
    close(fd);
    return -1;
  }
  fprintf(stderr, "neo daemon: listening on %s\n", socket_path);

  char *system_prompt = malloc(SYSTEM_MAX);
  char *line_buf = malloc(LINE_MAX);
  if (!system_prompt || !line_buf) {
    free(system_prompt);
    free(line_buf);
    close(fd);
    return -1;
  }

  for (;;) {
    int client = accept(fd, NULL, NULL);
    if (client < 0) continue;
    line_buf[0] = '\0';
    size_t n = 0;
    while (n < LINE_MAX - 1) {
      char c;
      if (read(client, &c, 1) != 1) break;
      if (c == '\n' || c == '\r') break;
      line_buf[n++] = c;
    }
    line_buf[n] = '\0';
    if (n > 0) {
      build_system_prompt(conf, line_buf, system_prompt, SYSTEM_MAX);
      if (debug) daemon_debug_print(conf, system_prompt, line_buf);
      llm_response_t resp = {0};
      if (do_one_turn(conf, system_prompt, line_buf, &resp) == 0 && resp.data && resp.size) {
        write(client, resp.data, resp.size);
        if (resp.size > 0 && resp.data[resp.size - 1] != '\n') write(client, "\n", 1);
        session_append("user", line_buf);
        session_append("assistant", resp.data);
        session_trim_to(conf->session_max_turns > 0 ? conf->session_max_turns : 10);
      }
      llm_response_free(&resp);
    }
    close(client);
  }
  free(system_prompt);
  free(line_buf);
  close(fd);
  return 0;
}
#else
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug) {
  (void)conf;
  (void)socket_path;
  (void)debug;
  fprintf(stderr, "neo: Unix socket not supported on this platform\n");
  return -1;
}
#endif

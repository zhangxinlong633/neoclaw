/*
 * Skills: inject index (short) for all, full content only when user message matches.
 * Reduces system prompt size when many skills are configured.
 */
#include "skills.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKILL_INDEX_CHARS 400
#define SKILL_FULL_CHARS  32000
#define TMP_BUF_SIZE      65536

/* Get directory name from path, e.g. "skills/nanjing/SKILL.md" -> "nanjing". */
static void path_to_skill_name(const char *path, char *name_out, size_t name_max) {
  name_out[0] = '\0';
  const char *last_slash = strrchr(path, '/');
  if (!last_slash) return;
  const char *prev_slash = last_slash;
  while (prev_slash > path && prev_slash[-1] != '/') prev_slash--;
  const char *segment = (prev_slash > path && prev_slash[0] == '/') ? prev_slash + 1 : path;
  size_t len = (size_t)(last_slash - segment);
  if (len >= name_max) len = name_max - 1;
  memcpy(name_out, segment, len);
  name_out[len] = '\0';
}

/* Keywords (null-separated) for extra matching; path segment is always matched case-insensitive. */
static const char* get_keywords_for_name(const char *name) {
  if (strcmp(name, "nanjing") == 0) return "南京";
  if (strcmp(name, "summarize") == 0) return "总结";
  if (strcmp(name, "note") == 0) return "记住\0笔记";
  if (strcmp(name, "todo") == 0) return "待办\0任务";
  if (strcmp(name, "me") == 0) return "谁\0身份\0介绍";
  if (strcmp(name, "explain") == 0) return "解释\0什么意思\0怎么用";
  if (strcmp(name, "code") == 0) return "代码\0脚本\0命令\0怎么写";
  if (strcmp(name, "translate") == 0) return "翻译\0译成";
  return "";
}

static int keyword_in_message(const char *user_message, const char *keyword) {
  if (!user_message || !keyword || !*keyword) return 0;
  return strstr(user_message, keyword) != NULL;
}

/* 1 if we should load full skill content for this path given user_message. */
static int skill_matches_user(const char *path, const char *user_message) {
  char name[64];
  path_to_skill_name(path, name, sizeof(name));
  if (!*name) return 0;

  static char lower_buf[65536];
  size_t ulen = user_message ? strlen(user_message) : 0;
  if (ulen >= sizeof(lower_buf)) ulen = sizeof(lower_buf) - 1;
  for (size_t i = 0; i < ulen; i++)
    lower_buf[i] = (unsigned char)user_message[i] < 128 ? (char)tolower((unsigned char)user_message[i]) : user_message[i];
  lower_buf[ulen] = '\0';

  char name_lower[64];
  size_t nlen = strlen(name);
  if (nlen >= sizeof(name_lower)) nlen = sizeof(name_lower) - 1;
  for (size_t i = 0; i <= nlen; i++)
    name_lower[i] = (char)tolower((unsigned char)name[i]);
  if (strstr(lower_buf, name_lower)) return 1;

  const char *kw = get_keywords_for_name(name);
  while (*kw) {
    if (keyword_in_message(user_message, kw)) return 1;
    kw += strlen(kw) + 1;
  }
  return 0;
}

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

void skills_append_to_system_prompt(agent_config_t *conf, const char *user_message, char *dest, size_t cap, int priority_filter) {
  char *tmp = malloc(TMP_BUF_SIZE);
  if (!tmp) return;

  for (int i = 0; i < conf->skills.path_count; i++) {
    int p = (conf->skills.priority && i < conf->skills.path_count) ? conf->skills.priority[i] : 0;
    if (priority_filter >= 0 && (priority_filter ? (p != 1) : (p != 0))) continue; /* -1: all; 1: only high; 0: only normal */
    const char *path = conf->skills.paths[i];
    /* High-priority skills always load full content so key data (e.g. 必引) is never truncated */
    int full = (priority_filter == 1 && p == 1) ? 1 : skill_matches_user(path, user_message);
    if (!full && conf->skills.unmatched) continue; /* skip this skill to save context */
    size_t max_c = full ? (size_t)SKILL_FULL_CHARS : (size_t)SKILL_INDEX_CHARS;
    if (read_file_into(tmp, TMP_BUF_SIZE, path, max_c) > 0) {
      if (!full && strlen(tmp) > (size_t)SKILL_INDEX_CHARS)
        tmp[SKILL_INDEX_CHARS] = '\0';
      append_section(dest, cap, "## Skill: ", path, tmp);
    }
  }
  free(tmp);
}

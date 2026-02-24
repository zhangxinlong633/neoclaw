#include "llm.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  llm_response_t *r = (llm_response_t *)userdata;
  size_t total = size * nmemb;
  char *n = realloc(r->data, r->size + total + 1);
  if (!n) return 0;
  r->data = n;
  memcpy(r->data + r->size, ptr, total);
  r->size += total;
  r->data[r->size] = '\0';
  return total;
}

static void json_escape(const char *in, char *out, size_t out_max) {
  size_t j = 0;
  for (; in && *in && j < out_max - 2; in++) {
    if (*in == '\\' || *in == '"' || *in == '\n' || *in == '\r' || *in == '\t') {
      if (j < out_max - 2) { out[j++] = '\\'; out[j++] = (*in == '\n') ? 'n' : (*in == '\r') ? 'r' : (*in == '\t') ? 't' : *in; }
    } else
      out[j++] = *in;
  }
  out[j] = '\0';
}

void llm_response_free(llm_response_t *r) {
  if (!r) return;
  free(r->data);
  r->data = NULL;
  r->size = 0;
}

static int extract_content_from_json(const char *json, llm_response_t *out) {
  const char *needle = "\"content\":";
  const char *p = strstr(json, needle);
  if (!p) {
    needle = "\"content\": ";
    p = strstr(json, needle);
  }
  if (!p) return -1;
  p = strchr(p + strlen(needle), '"');
  if (!p) return -1;
  p++;
  const char *end = p;
  while (*end && !(end[0] == '"' && end[-1] != '\\')) end++;
  size_t len = (size_t)(end - p);
  out->data = malloc(len + 1);
  if (!out->data) return -1;
  memcpy(out->data, p, len);
  out->data[len] = '\0';
  out->size = len;
  for (char *q = out->data; *q; q++) {
    if (q[0] == '\\' && (q[1] == 'n' || q[1] == 'r' || q[1] == 't' || q[1] == '"' || q[1] == '\\')) {
      if (q[1] == 'n') *q = '\n'; else if (q[1] == 'r') *q = '\r'; else if (q[1] == 't') *q = '\t'; else *q = q[1];
      memmove(q + 1, q + 2, strlen(q + 2) + 1);
    }
  }
  return 0;
}

static int do_request(CURL *curl, const char *body, llm_response_t *out, long *http_code) {
  out->data = NULL;
  out->size = 0;
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) return -1;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);
  return 0;
}

int llm_chat(const char *base_url, const char *model, const char *api_key,
             int max_tokens, double temperature,
             const char *system_prompt, const char *user_message,
             llm_response_t *out) {
  out->data = NULL;
  out->size = 0;

  if (max_tokens <= 0) max_tokens = 4096;
  if (max_tokens > 16384) max_tokens = 16384; /* cap to avoid provider 502 */
  if (temperature < 0.0 || temperature > 2.0) temperature = 0.7;

  char sys_esc[65536];
  char usr_esc[32768];
  json_escape(system_prompt ? system_prompt : "", sys_esc, sizeof(sys_esc));
  json_escape(user_message ? user_message : "", usr_esc, sizeof(usr_esc));

  char url[1024];
  snprintf(url, sizeof(url), "%s/chat/completions", base_url);

  char body[128 * 1024];
  int n = snprintf(body, sizeof(body),
    "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},{\"role\":\"user\",\"content\":\"%s\"}],\"max_tokens\":%d,\"temperature\":%.2f}",
    model ? model : "qwen3:8b", sys_esc, usr_esc, max_tokens, temperature);
  if (n < 0 || (size_t)n >= sizeof(body)) return -1;

  CURL *curl = curl_easy_init();
  if (!curl) return -1;

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  if (api_key && api_key[0]) {
    char auth[1024];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth);
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

  long code = 0;
  int err = do_request(curl, body, out, &code);

  if (err == 0 && (code == 429 || code == 503 || (code >= 500 && code < 600))) {
    llm_response_free(out);
    struct timespec ts = { 1, 0 };
    nanosleep(&ts, NULL);
    err = do_request(curl, body, out, &code);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (err != 0) {
    llm_response_free(out);
    return -1;
  }
  if (code != 200) {
    if (out->data && out->size) fprintf(stderr, "neo: LLM HTTP %ld: %.*s\n", code, (int)(out->size > 512 ? 512 : out->size), out->data);
    llm_response_free(out);
    return -1;
  }
  if (!out->data) { llm_response_free(out); return -1; }
  llm_response_t extracted = {0};
  if (extract_content_from_json(out->data, &extracted) == 0) {
    llm_response_free(out);
    *out = extracted;
  }
  return 0;
}

int llm_chat_messages(const char *base_url, const char *model, const char *api_key,
                      int max_tokens, double temperature,
                      const char *system_prompt,
                      const llm_message_t *messages, int n_messages,
                      llm_response_t *out) {
  out->data = NULL;
  out->size = 0;
  if (max_tokens <= 0) max_tokens = 4096;
  if (max_tokens > 16384) max_tokens = 16384;
  if (temperature < 0.0 || temperature > 2.0) temperature = 0.7;

  char sys_esc[65536];
  json_escape(system_prompt ? system_prompt : "", sys_esc, sizeof(sys_esc));

  static char body_buf[512 * 1024];
  int off = snprintf(body_buf, sizeof(body_buf),
    "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":\"%s\"}",
    model ? model : "qwen3:8b", sys_esc);
  if (off < 0 || off >= (int)sizeof(body_buf)) return -1;

  char esc_buf[32768];
  for (int i = 0; i < n_messages && messages[i].role && messages[i].content; i++) {
    json_escape(messages[i].content, esc_buf, sizeof(esc_buf));
    const char *role = (messages[i].role && strcmp(messages[i].role, "assistant") == 0) ? "assistant" : "user";
    int n = snprintf(body_buf + off, (size_t)(sizeof(body_buf) - off),
      ",{\"role\":\"%s\",\"content\":\"%s\"}", role, esc_buf);
    if (n < 0 || off + n >= (int)sizeof(body_buf)) break;
    off += n;
  }
  int nn = snprintf(body_buf + off, (size_t)(sizeof(body_buf) - off),
    "],\"max_tokens\":%d,\"temperature\":%.2f}", max_tokens, temperature);
  if (nn < 0 || off + nn >= (int)sizeof(body_buf)) return -1;

  CURL *curl = curl_easy_init();
  if (!curl) return -1;

  char url[1024];
  snprintf(url, sizeof(url), "%s/chat/completions", base_url);

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  if (api_key && api_key[0]) {
    char auth[1024];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth);
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_buf);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

  long code = 0;
  int err = do_request(curl, body_buf, out, &code);
  if (err == 0 && (code == 429 || code == 503 || (code >= 500 && code < 600))) {
    llm_response_free(out);
    struct timespec ts = { 1, 0 };
    nanosleep(&ts, NULL);
    err = do_request(curl, body_buf, out, &code);
  }
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (err != 0 || code != 200) {
    if (out->data && out->size) fprintf(stderr, "neo: LLM HTTP %ld: %.*s\n", code, (int)(out->size > 512 ? 512 : out->size), out->data);
    llm_response_free(out);
    return -1;
  }
  if (!out->data) { llm_response_free(out); return -1; }
  llm_response_t extracted = {0};
  if (extract_content_from_json(out->data, &extracted) == 0) {
    llm_response_free(out);
    *out = extracted;
  }
  return 0;
}

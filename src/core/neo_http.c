/*
 * neo_http：BearHttpsClient 的薄封装。
 * 不把 vendor 头文件泄漏到 llm / agent_tools。
 */
#include "neo_http.h"
#include "BearHttpsClient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void neo_http_response_free(neo_http_response_t *r) {
  if (!r) return;
  free(r->body);
  r->body = NULL;
  r->body_len = 0;
  r->status = 0;
}

static void neo_http_response_clear(neo_http_response_t *out) {
  if (!out) return;
  out->body = NULL;
  out->body_len = 0;
  out->status = 0;
}

/* Bear 的 connection_timeout 以毫秒计；默认仅 2s，LLM/工具必须显式拉长。 */
static void neo_http_apply_timeout(BearHttpsRequest *req, int timeout_sec) {
  long ms;
  if (!req) return;
  if (timeout_sec < 1) timeout_sec = 30;
  if (timeout_sec > 600) timeout_sec = 600;
  ms = (long)timeout_sec * 1000L;
  req->connection_timeout = ms;
}

static int neo_http_copy_body(BearHttpsResponse *resp, size_t max_body, neo_http_response_t *out) {
  const char *body;
  size_t len;
  size_t copy_len;
  char *dup;

  body = BearHttpsResponse_read_body_str(resp);
  if (BearHttpsResponse_error(resp)) {
    fprintf(stderr, "neo http: read body: %s\n",
            BearHttpsResponse_get_error_msg(resp) ? BearHttpsResponse_get_error_msg(resp) : "unknown");
    return -1;
  }
  if (!body) body = "";
  len = strlen(body);
  copy_len = len;
  if (max_body > 0 && copy_len > max_body) copy_len = max_body;
  dup = malloc(copy_len + 1);
  if (!dup) return -1;
  memcpy(dup, body, copy_len);
  dup[copy_len] = '\0';
  out->body = dup;
  out->body_len = copy_len;
  return 0;
}

static int neo_http_finish(BearHttpsRequest *req, BearHttpsResponse *resp, size_t max_body,
                           neo_http_response_t *out) {
  int rc = -1;
  if (!resp) {
    fprintf(stderr, "neo http: fetch returned NULL\n");
    goto done;
  }
  if (BearHttpsResponse_error(resp)) {
    fprintf(stderr, "neo http: %s\n",
            BearHttpsResponse_get_error_msg(resp) ? BearHttpsResponse_get_error_msg(resp) : "request failed");
    goto done;
  }
  out->status = (long)BearHttpsResponse_get_status_code(resp);
  if (neo_http_copy_body(resp, max_body, out) != 0) goto done;
  rc = 0;
done:
  if (req) BearHttpsRequest_free(req);
  if (resp) BearHttpsResponse_free(resp);
  return rc;
}

int neo_http_get(const char *url, size_t max_body, int timeout_sec, neo_http_response_t *out) {
  BearHttpsRequest *req;
  BearHttpsResponse *resp;

  if (!url || !out) return -1;
  neo_http_response_clear(out);
  req = newBearHttpsRequest(url);
  if (!req) return -1;
  neo_http_apply_timeout(req, timeout_sec);
  BearHttpsRequest_set_method(req, "GET");
  /* 与旧 curl FOLLOWLOCATION=0 对齐：禁止自动跟随。 */
  BearHttpsRequest_set_max_redirections(req, 0);
  BearHttpsRequest_add_header(req, "User-Agent", "neo-tools/1");
  resp = BearHttpsRequest_fetch(req);
  return neo_http_finish(req, resp, max_body, out);
}

int neo_http_post_json(const char *url, const char *bearer_or_null, const char *json_body,
                       int timeout_sec, neo_http_response_t *out) {
  BearHttpsRequest *req;
  BearHttpsResponse *resp;
  char *body_copy;

  if (!url || !json_body || !out) return -1;
  neo_http_response_clear(out);
  req = newBearHttpsRequest(url);
  if (!req) return -1;
  neo_http_apply_timeout(req, timeout_sec);
  BearHttpsRequest_set_method(req, "POST");
  BearHttpsRequest_add_header(req, "Content-Type", "application/json");
  BearHttpsRequest_add_header(req, "Accept", "application/json");
  if (bearer_or_null && bearer_or_null[0]) {
    char auth[1100];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer_or_null);
    BearHttpsRequest_add_header(req, "Authorization", auth);
  }
  /* COPY：json_body 生命周期由调用方管理，库内自备副本。 */
  body_copy = strdup(json_body);
  if (!body_copy) {
    BearHttpsRequest_free(req);
    return -1;
  }
  BearHttpsRequest_send_body_str_with_ownership_control(req, body_copy, BEARSSL_HTTPS_GET_OWNERSHIP);
  resp = BearHttpsRequest_fetch(req);
  return neo_http_finish(req, resp, 0, out);
}

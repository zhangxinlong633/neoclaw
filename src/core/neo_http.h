#ifndef NEO_HTTP_H
#define NEO_HTTP_H

#include <stddef.h>

/*
 * 轻量 HTTPS 门面：对业务隐藏 BearHttpsClient。
 * 仅 neo_http.c 依赖 vendor；llm / http_get 只调本头文件。
 */

typedef struct neo_http_response {
  char *body;     /* 堆上副本，可为 NULL；调用方 neo_http_response_free */
  size_t body_len;
  long status;    /* HTTP 状态码；传输失败时为 0 */
} neo_http_response_t;

void neo_http_response_free(neo_http_response_t *r);

/*
 * HTTPS GET。成功返回 0（含非 2xx，此时 status/body 仍填好）；
 * 传输层失败返回 -1。max_body>0 时截断 body；timeout_sec 映射为连接超时（毫秒）。
 */
int neo_http_get(const char *url, size_t max_body, int timeout_sec, neo_http_response_t *out);

/*
 * HTTPS POST，Content-Type: application/json；可选 Bearer。
 * 语义同 neo_http_get。
 */
int neo_http_post_json(const char *url, const char *bearer_or_null, const char *json_body,
                       int timeout_sec, neo_http_response_t *out);

#endif

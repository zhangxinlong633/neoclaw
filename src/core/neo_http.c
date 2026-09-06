/*
 * neo_http：BearHttpsClient 的薄封装。
 * 不把 vendor 头文件泄漏到 llm / agent_tools。
 *
 * DNS：库默认走 DoH（dns.google / nextdns），国内等网络常失败；
 * 上游 BEARSSL_USSE_GET_ADDRINFO 路径在 darwin 上 TLS 易 BR_ERR_IO。
 * 因此在此用系统 getaddrinfo 解析后 set_known_ips，走库内 IPv4 直连。
 */
#include "neo_http.h"
#include "BearHttpsClient.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

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

/* Bear 的 connection_timeout 以毫秒计（IPv4 connect 路径）；默认仅 2s。 */
static void neo_http_apply_timeout(BearHttpsRequest *req, int timeout_sec) {
  long ms;
  if (!req) return;
  if (timeout_sec < 1) timeout_sec = 30;
  if (timeout_sec > 600) timeout_sec = 600;
  ms = (long)timeout_sec * 1000L;
  req->connection_timeout = ms;
}

/*
 * 从 https://host[:port]/path 抽出 host 到 out（不含端口）。
 * 返回 0 成功。
 */
static int neo_http_url_host(const char *url, char *out, size_t out_sz) {
  const char *p, *end;
  size_t n;
  if (!url || !out || out_sz < 2) return -1;
  p = url;
  if (!strncmp(p, "https://", 8))
    p += 8;
  else if (!strncmp(p, "http://", 7))
    p += 7;
  else
    return -1;
  if (*p == '[') return -1; /* 不支持 IPv6 literal */
  end = p;
  while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#') end++;
  n = (size_t)(end - p);
  if (n == 0 || n >= out_sz) return -1;
  memcpy(out, p, n);
  out[n] = '\0';
  return 0;
}

/*
 * 系统解析 A 记录，钉到 known_ips，避免 DoH。
 * ip_storage / ips 须在 fetch 完成前保持有效。
 */
static int neo_http_pin_system_dns(BearHttpsRequest *req, const char *url, char *ip_storage,
                                   size_t ip_storage_sz, const char **ips, int *ips_n) {
  char host[256];
  struct addrinfo hints, *res = NULL, *ai;
  int ga;

  *ips_n = 0;
  if (neo_http_url_host(url, host, sizeof(host)) != 0) return -1;

  /* 已是字面 IPv4：直接钉住。 */
  {
    struct in_addr tmp;
    if (inet_pton(AF_INET, host, &tmp) == 1) {
      if (strlen(host) >= ip_storage_sz) return -1;
      memcpy(ip_storage, host, strlen(host) + 1);
      ips[0] = ip_storage;
      *ips_n = 1;
      BearHttpsRequest_set_known_ips(req, ips, 1);
      return 0;
    }
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  ga = getaddrinfo(host, NULL, &hints, &res);
  if (ga != 0 || !res) {
    fprintf(stderr, "neo http: DNS resolve failed for %s: %s\n", host,
            ga != 0 ? gai_strerror(ga) : "no address");
    return -1;
  }
  for (ai = res; ai; ai = ai->ai_next) {
    if (ai->ai_family != AF_INET) continue;
    if (!inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, ip_storage,
                   (socklen_t)ip_storage_sz)) {
      freeaddrinfo(res);
      return -1;
    }
    ips[0] = ip_storage;
    *ips_n = 1;
    BearHttpsRequest_set_known_ips(req, ips, 1);
    freeaddrinfo(res);
    return 0;
  }
  freeaddrinfo(res);
  fprintf(stderr, "neo http: no IPv4 for %s\n", host);
  return -1;
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
  char ip_storage[64];
  const char *ips[1];
  int ips_n = 0;

  if (!url || !out) return -1;
  neo_http_response_clear(out);
  req = newBearHttpsRequest(url);
  if (!req) return -1;
  neo_http_apply_timeout(req, timeout_sec);
  if (neo_http_pin_system_dns(req, url, ip_storage, sizeof(ip_storage), ips, &ips_n) != 0) {
    BearHttpsRequest_free(req);
    return -1;
  }
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
  char ip_storage[64];
  const char *ips[1];
  int ips_n = 0;

  if (!url || !json_body || !out) return -1;
  neo_http_response_clear(out);
  req = newBearHttpsRequest(url);
  if (!req) return -1;
  neo_http_apply_timeout(req, timeout_sec);
  if (neo_http_pin_system_dns(req, url, ip_storage, sizeof(ip_storage), ips, &ips_n) != 0) {
    BearHttpsRequest_free(req);
    return -1;
  }
  BearHttpsRequest_set_method(req, "POST");
  BearHttpsRequest_add_header(req, "Content-Type", "application/json");
  BearHttpsRequest_add_header(req, "Accept", "application/json");
  if (bearer_or_null && bearer_or_null[0]) {
    char auth[1100];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer_or_null);
    BearHttpsRequest_add_header(req, "Authorization", auth);
  }
  body_copy = strdup(json_body);
  if (!body_copy) {
    BearHttpsRequest_free(req);
    return -1;
  }
  BearHttpsRequest_send_body_str_with_ownership_control(req, body_copy, BEARSSL_HTTPS_GET_OWNERSHIP);
  resp = BearHttpsRequest_fetch(req);
  return neo_http_finish(req, resp, 0, out);
}

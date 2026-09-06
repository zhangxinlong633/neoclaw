# Replace libcurl with BearHttpsClient (neo_http facade)

| Attribute | Value |
|-----------|-------|
| Date | 2026-09-06 |
| Status | Approved |
| Approach | Thin `neo_http` wrapper; compile `BearHttpsClientOne.c` as vendor `.o` |

## Goal

Remove system dependency on **libcurl**. Use vendored **BearHttpsClient** (`src/vendor/BearHttpsClientOne.c` + `.h`) for HTTPS. Keep Neo JSON on **yyjson**.

## Non-goals

- Dual backend / keep curl behind `#ifdef`
- Replacing yyjson with Bear’s embedded cJSON
- Changing OpenAI request/response schema

## Design

### Vendor

- Already present: `BearHttpsClientOne.c`, `BearHttpsClient.h`
- Compile One.c alone (like yyjson); do **not** `#include` the amalgamation from multiple TUs
- Call sites must **not** include Bear headers except inside the facade `.c`
- Update `src/vendor/README.md`

### Facade: `src/core/neo_http.h` + `neo_http.c`

Minimal API (C99):

- `neo_http_response_t { char *body; size_t body_len; long status; }` + `neo_http_response_free`
- `neo_http_get(const char *url, size_t max_body, int timeout_sec, neo_http_response_t *out)`
- `neo_http_post_json(const char *url, const char *bearer_token_or_null, const char *json_body, int timeout_sec, neo_http_response_t *out)`

Implementation uses `newBearHttpsRequest` / `set_method` / `add_header` / `send_body_str` / `fetch` / `read_body_str` / status / free. Map errors to return `-1` and optional stderr snippet. Prefer no redirects for GET tool (match prior `FOLLOWLOCATION 0` if API allows `set_max_redirections(0)`).

### Call sites

| File | Change |
|------|--------|
| `src/llm/llm.c` | Use `neo_http_post_json` for `/chat/completions`; keep retry on 429/5xx |
| `src/capability/agent_tools.c` | `tool_http_get` → `neo_http_get`; drop curl write callback |
| `Makefile` | Add vendor One.c to `SRC`; `LDFLAGS` without `-lcurl`; silence vendor warnings |
| Tests that link `llm.c` / `agent_tools.c` | Drop `-lcurl` |

### Docs

`AGENTS.md`, `CLAUDE.md`, `src/llm/README.md`, and user docs that claim “libcurl only”.

## Verification

`make` && `make test`. Optional live LLM call if config present.

## Risks

BearHttpsClient is alpha; macOS not prominently listed—fix link/TLS on darwin first. Amalgamation is large (~3MB); keep as vendor blob, upgrade by whole-file replace.

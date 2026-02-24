#ifndef NEO_LLM_H
#define NEO_LLM_H

#include <stddef.h>

typedef struct {
  char *data;
  size_t size;
} llm_response_t;

void llm_response_free(llm_response_t *r);

int llm_chat(const char *base_url, const char *model, const char *api_key,
             int max_tokens, double temperature,
             const char *system_prompt, const char *user_message,
             llm_response_t *out);

typedef struct {
  const char *role;
  const char *content;
} llm_message_t;

int llm_chat_messages(const char *base_url, const char *model, const char *api_key,
                      int max_tokens, double temperature,
                      const char *system_prompt,
                      const llm_message_t *messages, int n_messages,
                      llm_response_t *out);

#endif

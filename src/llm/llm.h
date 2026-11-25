#ifndef STEAMSCRIBE_LLM_LLM_H
#define STEAMSCRIBE_LLM_LLM_H

#include "util/error.h"
#include "util/strbuf.h"

typedef struct llm_cfg_t
{
    const char *model;
    const char *base_url; /* openai-compatible only; empty => OpenAI's default endpoint */
    const char *api_key;  /* may be empty -- fine for local backends (Ollama, LM Studio, ...) */
} llm_cfg_t;

typedef int (*llm_complete_fn)(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err);

typedef struct llm_provider_t
{
    const char *name;
    llm_complete_fn complete;
} llm_provider_t;

/* Looks up a provider by name: "anthropic", "openai" (OpenAI-compatible --
 * also covers Ollama, LM Studio, OpenRouter and friends via llm_cfg_t.base_url),
 * or "fake" (canned responses, for tests). Returns NULL if `name` is unknown. */
const llm_provider_t *llm_provider_find(const char *name);

#endif

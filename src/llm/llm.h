#ifndef STEAMSCRIBE_LLM_LLM_H
#define STEAMSCRIBE_LLM_LLM_H

#include "util/error.h"
#include "util/strbuf.h"

typedef struct llm_cfg_t
{
    const char *model;
    const char *base_url; /* openai/ollama only; empty => that provider's default endpoint */
    const char *api_key;  /* may be empty -- fine for local backends (Ollama, LM Studio, ...) */
    int num_ctx;          /* ollama only; 0 => let Ollama use the model's own default */
} llm_cfg_t;

typedef int (*llm_complete_fn)(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err);

/* Like llm_complete_fn, but asks the provider to constrain its output to
 * `json_schema` (a JSON Schema object, as a string) when it can enforce
 * that server-side. */
typedef int (*llm_complete_json_fn)(const llm_cfg_t *cfg, const char *system, const char *user, const char *json_schema, strbuf_t *out, err_t *err);

typedef struct llm_provider_t
{
    const char *name;
    llm_complete_fn complete;
    llm_complete_json_fn complete_json; /* optional; NULL if this provider can't enforce a schema */
} llm_provider_t;

/* Looks up a provider by name: "anthropic", "openai" (OpenAI-compatible --
 * covers LM Studio, OpenRouter and friends via llm_cfg_t.base_url), "ollama"
 * (Ollama's native API -- num_ctx and JSON-schema enforcement), or "fake"
 * (canned responses, for tests). Returns NULL if `name` is unknown. */
const llm_provider_t *llm_provider_find(const char *name);

#endif

#ifndef STEAMSCRIBE_LLM_OPENAI_H
#define STEAMSCRIBE_LLM_OPENAI_H

#include "llm/llm.h"

/* OpenAI-compatible chat completions backend. llm_cfg_t.base_url also makes
 * this the path to local models: point it at Ollama (e.g.
 * http://localhost:11434/v1) or LM Studio's endpoint, or another compatible
 * server such as OpenRouter. No API key is required when talking to a
 * local server -- an empty llm_cfg_t.api_key simply omits the Authorization
 * header. */
extern const llm_provider_t llm_provider_openai;

#endif

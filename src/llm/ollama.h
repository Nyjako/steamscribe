#ifndef STEAMSCRIBE_LLM_OLLAMA_H
#define STEAMSCRIBE_LLM_OLLAMA_H

#include "llm/llm.h"

/* Ollama's native /api/chat, not the OpenAI-compatible one: this gets us
 * per-request num_ctx (the OpenAI-compatible endpoint's context size is
 * fixed by the model's Modelfile) and a JSON-schema-constrained `format`,
 * so most theme-extraction parse failures never happen server-side instead
 * of needing a client-side retry. "thinking" output is turned off
 * (`think: false`) so reasoning-style models don't need `<think>` stripping. */
extern const llm_provider_t llm_provider_ollama;

#endif

#ifndef STEAMSCRIBE_CONFIG_H
#define STEAMSCRIBE_CONFIG_H

#include <stdbool.h>

#include "util/arena.h"
#include "util/error.h"

typedef struct config_t
{
    /* [llm] */
    char *llm_provider;       /* "anthropic" | "openai" | "fake" */
    char *llm_model;
    char *llm_base_url;       /* openai-compatible only; empty => OpenAI's default endpoint.
                                * Point this at a local server (e.g. http://localhost:11434/v1
                                * for Ollama, or LM Studio's endpoint) to use a local model --
                                * no API key is required in that case. */

    /* API keys, read only from the environment, never from the config file. */
    char *anthropic_api_key;  /* ANTHROPIC_API_KEY, may be empty */
    char *openai_api_key;     /* OPENAI_API_KEY, may be empty (fine for local backends) */

    /* [steam] */
    char *steam_language;
    int steam_review_count;
    int steam_cache_ttl_hours;

    /* [review] */
    char *review_default_length; /* "short" | "medium" | "long" */
    char *review_default_tone;   /* "casual" | "neutral" | "enthusiastic" */

    bool verbose;
} config_t;

/* Loads ~/.config/steamscribe/config.ini (respecting XDG_CONFIG_HOME), creating
 * a default file there on first run. All returned strings are arena-allocated. */
int config_load(arena_t *arena, config_t *out, err_t *err);

#endif

#ifndef STEAMSCRIBE_CONFIG_H
#define STEAMSCRIBE_CONFIG_H

#include <stdbool.h>

#include "util/arena.h"
#include "util/error.h"

typedef struct config_t
{
    /* [llm] */
    char *llm_provider;       /* "anthropic" | "openai" | "ollama" | "fake" */
    char *llm_model;
    char *llm_base_url;       /* openai/ollama only; empty => that provider's default endpoint.
                                * For "openai", point this at a local server (e.g. LM Studio's
                                * endpoint) to use a local model -- no API key is required in
                                * that case. "ollama" defaults to http://localhost:11434 and
                                * talks to Ollama's own API, not the OpenAI-compatible one. */
    int llm_num_ctx;          /* ollama only; 0 => let Ollama use the model's own default */

    /* The Anthropic/OpenAI keys are read only from the environment, never
     * from the config file. */
    char *anthropic_api_key;  /* ANTHROPIC_API_KEY, may be empty */
    char *openai_api_key;     /* OPENAI_API_KEY, may be empty (fine for local backends) */

    /* STEAM_API_KEY (env) takes priority if set, same as the keys above --
     * steam_api_key_from_env says which source this run's value came from.
     * Otherwise it comes from config.ini's [steam] steam_api_key, which may
     * be pasted in as plain text: config_load() encrypts it at rest (see
     * util/secret_crypto.h) and rewrites the file the first time it sees a
     * plaintext value there. steam_api_key itself is always the plaintext,
     * for in-memory use this run; may be empty -- only needed for playtime. */
    char *steam_api_key;
    bool steam_api_key_from_env;

    /* [steam] */
    char *steam_language;
    int steam_review_count;
    int steam_cache_ttl_hours;
    char *steam_id;           /* your own SteamID64, may be empty -- only needed for
                                * playtime, paired with STEAM_API_KEY above. Not a
                                * secret (it's just a numeric account id), so unlike the
                                * API key it's fine to keep in config.ini. */

    /* [review] */
    char *review_default_length; /* "short" | "medium" | "long" */
    char *review_default_tone;   /* "casual" | "neutral" | "enthusiastic" */
    char *review_default_vocab;  /* "normal" | "simple" */

    bool verbose;
} config_t;

/* Loads ~/.config/steamscribe/config.ini (respecting XDG_CONFIG_HOME), creating
 * a default file there on first run. All returned strings are arena-allocated. */
int config_load(arena_t *arena, config_t *out, err_t *err);

/* Regenerates config.ini from `cfg`'s current values. The Anthropic/OpenAI
 * keys are never written (env-only). steam_api_key is written encrypted
 * at rest when it didn't come from the environment this run; left out
 * (as a commented example) when it's empty or env-sourced. */
int config_save(const config_t *cfg, err_t *err);

#endif

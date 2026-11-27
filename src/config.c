#include "config.h"

#include <ini.h>

#include <goblin/appdirs.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util/secret_crypto.h"

#define CONFIG_APP_NAME "steamscribe"

static const char *DEFAULT_CONFIG_INI =
    "[llm]\n"
    "; See docs/PROVIDERS.md for how to set each of these up (getting a key, picking a\n"
    "; model, ...) -- ollama is the default since it needs no API key and runs fully\n"
    "; locally; swap in one of the commented blocks below for a hosted provider instead.\n"
    "provider = ollama              ; anthropic | openai | ollama\n"
    "model    = qwen3.5-9b          ; swap for whatever local model you've pulled -- see\n"
    "                               ; docs/PROVIDERS.md for a few known to work well\n"
    "; num_ctx  = 8192              ; ollama only -- raise this if reviews get truncated\n"
    "; base_url = http://localhost:11434    ; ollama only; this is already the default\n"
    "\n"
    "; provider = anthropic         ; needs ANTHROPIC_API_KEY in your environment\n"
    "; model    = claude-haiku-5-5\n"
    "\n"
    "; provider = openai            ; needs OPENAI_API_KEY, unless base_url below points\n"
    "; model    = gpt-4o-mini       ; at a local OpenAI-compatible server (LM Studio, ...)\n"
    "; base_url = http://localhost:1234/v1\n"
    "\n"
    "[steam]\n"
    "language    = english\n"
    "review_count = 200            ; reviews to analyze (half positive, half negative)\n"
    "cache_ttl_hours = 24\n"
    "; steam_id = 76561198000000000  ; your own SteamID64 -- lets the review mention your\n"
    "; playtime for this game. Also needs a free Steam Web API key, either in the\n"
    "; STEAM_API_KEY env var, or pasted in plain below (it's encrypted at rest on the\n"
    "; next launch -- see docs/DESIGN.md). Both steam_id and a key are optional; the\n"
    "; review is generated without playtime if either is unset.\n"
    "; steam_api_key = ...\n"
    "\n"
    "[review]\n"
    "default_length = short        ; short | medium | long\n"
    "default_tone   = casual       ; casual | neutral | enthusiastic\n"
    "default_vocab  = normal       ; normal | simple (plain, everyday words)\n";

typedef struct raw_config_t
{
    char *llm_provider;
    char *llm_model;
    char *llm_base_url;
    char *llm_num_ctx;
    char *steam_language;
    char *steam_review_count;
    char *steam_cache_ttl_hours;
    char *steam_id;
    char *steam_api_key;
    char *review_default_length;
    char *review_default_tone;
    char *review_default_vocab;
} raw_config_t;

static int steamscribe_ini_handler(void *user, const char *section, const char *name, const char *value)
{
    raw_config_t *cfg = (raw_config_t *)user;

#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)

    if (MATCH("llm", "provider")) cfg->llm_provider = strdup(value);
    else if (MATCH("llm", "model")) cfg->llm_model = strdup(value);
    else if (MATCH("llm", "base_url")) cfg->llm_base_url = strdup(value);
    else if (MATCH("llm", "num_ctx")) cfg->llm_num_ctx = strdup(value);
    else if (MATCH("steam", "language")) cfg->steam_language = strdup(value);
    else if (MATCH("steam", "review_count")) cfg->steam_review_count = strdup(value);
    else if (MATCH("steam", "cache_ttl_hours")) cfg->steam_cache_ttl_hours = strdup(value);
    else if (MATCH("steam", "steam_id")) cfg->steam_id = strdup(value);
    else if (MATCH("steam", "steam_api_key")) cfg->steam_api_key = strdup(value);
    else if (MATCH("review", "default_length")) cfg->review_default_length = strdup(value);
    else if (MATCH("review", "default_tone")) cfg->review_default_tone = strdup(value);
    else if (MATCH("review", "default_vocab")) cfg->review_default_vocab = strdup(value);

#undef MATCH

    return 1;
}

/* Returns a malloc'd path to config.ini (creating the config directory if
 * needed), or NULL on failure. The caller frees it. */
static char *get_config_path(err_t *err)
{
    char *config_dir = goblin_appdir_config_dir(CONFIG_APP_NAME);
    if (!config_dir)
    {
        err_set(err, -1, "could not determine config directory");
        return NULL;
    }

    if (goblin_appdir_mkdir_p(config_dir) != 0)
    {
        err_set(err, -1, "could not create config dir %s", config_dir);
        free(config_dir);
        return NULL;
    }

    char *path = goblin_appdir_join(config_dir, "config.ini");
    free(config_dir);
    if (!path) err_set(err, -1, "could not build config path");

    return path;
}

static void create_default_config_if_missing(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return;

    FILE *fp = fopen(path, "w");
    if (!fp) return;

    fputs(DEFAULT_CONFIG_INI, fp);
    fclose(fp);
}

static void free_raw_config(raw_config_t *raw)
{
    free(raw->llm_provider);
    free(raw->llm_model);
    free(raw->llm_base_url);
    free(raw->llm_num_ctx);
    free(raw->steam_language);
    free(raw->steam_review_count);
    free(raw->steam_cache_ttl_hours);
    free(raw->steam_id);
    free(raw->steam_api_key);
    free(raw->review_default_length);
    free(raw->review_default_tone);
    free(raw->review_default_vocab);
}

int config_load(arena_t *arena, config_t *out, err_t *err)
{
    char *path = get_config_path(err);
    if (!path) return -1;

    create_default_config_if_missing(path);

    raw_config_t raw;
    memset(&raw, 0, sizeof(raw));
    ini_parse(path, steamscribe_ini_handler, &raw);
    free(path);

    out->llm_provider = arena_strdup(arena, raw.llm_provider ? raw.llm_provider : "ollama");
    out->llm_model = arena_strdup(arena, raw.llm_model ? raw.llm_model : "qwen3.5-9b");
    out->llm_base_url = arena_strdup(arena, raw.llm_base_url ? raw.llm_base_url : "");
    out->llm_num_ctx = raw.llm_num_ctx ? atoi(raw.llm_num_ctx) : 8192;
    out->steam_language = arena_strdup(arena, raw.steam_language ? raw.steam_language : "english");
    out->steam_review_count = raw.steam_review_count ? atoi(raw.steam_review_count) : 200;
    out->steam_cache_ttl_hours = raw.steam_cache_ttl_hours ? atoi(raw.steam_cache_ttl_hours) : 24;
    out->steam_id = arena_strdup(arena, raw.steam_id ? raw.steam_id : "");
    out->review_default_length = arena_strdup(arena, raw.review_default_length ? raw.review_default_length : "short");
    out->review_default_tone = arena_strdup(arena, raw.review_default_tone ? raw.review_default_tone : "casual");
    out->review_default_vocab = arena_strdup(arena, raw.review_default_vocab ? raw.review_default_vocab : "normal");

    const char *anthropic_key = getenv("ANTHROPIC_API_KEY");
    const char *openai_key = getenv("OPENAI_API_KEY");
    const char *steam_key_env = getenv("STEAM_API_KEY");
    out->anthropic_api_key = arena_strdup(arena, anthropic_key ? anthropic_key : "");
    out->openai_api_key = arena_strdup(arena, openai_key ? openai_key : "");

    bool needs_encrypt_rewrite = false;
    if (steam_key_env && steam_key_env[0])
    {
        out->steam_api_key = arena_strdup(arena, steam_key_env);
        out->steam_api_key_from_env = true;
    }
    else if (raw.steam_api_key && raw.steam_api_key[0])
    {
        out->steam_api_key_from_env = false;

        if (secret_crypto_is_encrypted(raw.steam_api_key))
        {
            uint8_t key[SECRET_CRYPTO_KEY_LEN];
            char *decrypted = NULL;
            err_t crypto_err;
            err_clear(&crypto_err);
            if (secret_crypto_load_or_create_key(key, &crypto_err) == 0 && secret_crypto_decrypt_from_text(arena, key, raw.steam_api_key, &decrypted) == 0)
                out->steam_api_key = decrypted;
            else
                out->steam_api_key = arena_strdup(arena, ""); /* corrupted/undecryptable -- best-effort, never fatal */
        }
        else
        {
            /* Freshly pasted plaintext -- use it as-is this run, and
             * rewrite config.ini with the encrypted form below so it
             * doesn't keep sitting there in plaintext past this launch. */
            out->steam_api_key = arena_strdup(arena, raw.steam_api_key);
            needs_encrypt_rewrite = true;
        }
    }
    else
    {
        out->steam_api_key = arena_strdup(arena, "");
        out->steam_api_key_from_env = false;
    }

    free_raw_config(&raw);

    out->verbose = false;

    if (needs_encrypt_rewrite)
    {
        err_t save_err;
        err_clear(&save_err);
        config_save(out, &save_err); /* best-effort -- worst case, tried again next launch */
    }

    return 0;
}

/* Regenerates config.ini from `cfg`'s current values. The Anthropic/OpenAI
 * keys are never written (env-only, same as before); steam_api_key is
 * written encrypted at rest (see util/secret_crypto.h) unless it came from
 * the environment this run, in which case it's left out just like the
 * other keys. This replaces whatever was in the file, including any
 * comments the user added, which is an acceptable trade-off for a config
 * file the TUI's Settings screen can also write. */
int config_save(const config_t *cfg, err_t *err)
{
    char *path = get_config_path(err);
    if (!path) return -1;

    FILE *fp = fopen(path, "w");
    free(path);
    if (!fp)
    {
        err_set(err, -1, "could not open config file for writing");
        return -1;
    }

    fprintf(fp, "[llm]\n");
    fprintf(fp, "; See docs/PROVIDERS.md for how to set up each provider (getting a key, picking a model, ...)\n");
    fprintf(fp, "provider = %s          ; anthropic | openai | ollama\n", cfg->llm_provider);
    fprintf(fp, "model    = %s\n", cfg->llm_model);
    if (cfg->llm_base_url[0]) fprintf(fp, "base_url = %s\n", cfg->llm_base_url);
    else fprintf(fp, "; base_url = http://localhost:1234/v1   ; for LM Studio (provider = openai); ollama defaults to http://localhost:11434\n");
    fprintf(fp, "num_ctx  = %d              ; ollama only -- raise this if reviews get truncated\n", cfg->llm_num_ctx);

    fprintf(fp, "\n[steam]\n");
    fprintf(fp, "language = %s\n", cfg->steam_language);
    fprintf(fp, "review_count = %d            ; reviews to analyze (half positive, half negative)\n", cfg->steam_review_count);
    fprintf(fp, "cache_ttl_hours = %d\n", cfg->steam_cache_ttl_hours);
    if (cfg->steam_id[0]) fprintf(fp, "steam_id = %s          ; your own SteamID64 -- also needs a Steam Web API key below\n", cfg->steam_id);
    else fprintf(fp, "; steam_id = 76561198000000000  ; your own SteamID64 -- lets the review mention your playtime; also needs a Steam Web API key below\n");

    if (cfg->steam_api_key_from_env)
    {
        fprintf(fp, "; steam_api_key is set via the STEAM_API_KEY environment variable -- not stored here\n");
    }
    else if (cfg->steam_api_key[0])
    {
        uint8_t key[SECRET_CRYPTO_KEY_LEN];
        err_t key_err;
        err_clear(&key_err);
        char *encrypted = (secret_crypto_load_or_create_key(key, &key_err) == 0) ? secret_crypto_encrypt_to_text(key, cfg->steam_api_key) : NULL;
        if (encrypted)
        {
            fprintf(fp, "steam_api_key = %s   ; encrypted at rest -- paste a plain key here and it's encrypted on next launch\n", encrypted);
            free(encrypted);
        }
        else
        {
            fprintf(fp, "; steam_api_key could not be encrypted -- not saved this time, try again\n");
        }
    }
    else
    {
        fprintf(fp, "; steam_api_key = ...            ; your free Steam Web API key -- paste it plain, it's encrypted on next launch\n");
    }

    fprintf(fp, "\n[review]\n");
    fprintf(fp, "default_length = %s        ; short | medium | long\n", cfg->review_default_length);
    fprintf(fp, "default_tone   = %s       ; casual | neutral | enthusiastic\n", cfg->review_default_tone);
    fprintf(fp, "default_vocab  = %s       ; normal | simple (plain, everyday words)\n", cfg->review_default_vocab);

    fclose(fp);
    return 0;
}

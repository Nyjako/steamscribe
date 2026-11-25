#include "config.h"

#include <ini.h>

#include <goblin/appdirs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_APP_NAME "steamscribe"

static const char *DEFAULT_CONFIG_INI =
    "[llm]\n"
    "provider = anthropic          ; anthropic | openai\n"
    "model    = claude-haiku-5-5\n"
    "; base_url = http://localhost:11434/v1   ; for Ollama / LM Studio (provider = openai)\n"
    "\n"
    "[steam]\n"
    "language    = english\n"
    "review_count = 200            ; reviews to analyze (half positive, half negative)\n"
    "cache_ttl_hours = 24\n"
    "\n"
    "[review]\n"
    "default_length = short        ; short | medium | long\n"
    "default_tone   = casual       ; casual | neutral | enthusiastic\n";

typedef struct raw_config_t
{
    char *llm_provider;
    char *llm_model;
    char *llm_base_url;
    char *steam_language;
    char *steam_review_count;
    char *steam_cache_ttl_hours;
    char *review_default_length;
    char *review_default_tone;
} raw_config_t;

static int steamscribe_ini_handler(void *user, const char *section, const char *name, const char *value)
{
    raw_config_t *cfg = (raw_config_t *)user;

#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)

    if (MATCH("llm", "provider")) cfg->llm_provider = strdup(value);
    else if (MATCH("llm", "model")) cfg->llm_model = strdup(value);
    else if (MATCH("llm", "base_url")) cfg->llm_base_url = strdup(value);
    else if (MATCH("steam", "language")) cfg->steam_language = strdup(value);
    else if (MATCH("steam", "review_count")) cfg->steam_review_count = strdup(value);
    else if (MATCH("steam", "cache_ttl_hours")) cfg->steam_cache_ttl_hours = strdup(value);
    else if (MATCH("review", "default_length")) cfg->review_default_length = strdup(value);
    else if (MATCH("review", "default_tone")) cfg->review_default_tone = strdup(value);

#undef MATCH

    return 1;
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
    free(raw->steam_language);
    free(raw->steam_review_count);
    free(raw->steam_cache_ttl_hours);
    free(raw->review_default_length);
    free(raw->review_default_tone);
}

int config_load(arena_t *arena, config_t *out, err_t *err)
{
    char *config_dir = goblin_appdir_config_dir(CONFIG_APP_NAME);
    if (!config_dir)
    {
        err_set(err, -1, "could not determine config directory");
        return -1;
    }

    if (goblin_appdir_mkdir_p(config_dir) != 0)
    {
        err_set(err, -1, "could not create config dir %s", config_dir);
        free(config_dir);
        return -1;
    }

    char *path = goblin_appdir_join(config_dir, "config.ini");
    free(config_dir);
    if (!path)
    {
        err_set(err, -1, "could not build config path");
        return -1;
    }

    create_default_config_if_missing(path);

    raw_config_t raw;
    memset(&raw, 0, sizeof(raw));
    ini_parse(path, steamscribe_ini_handler, &raw);
    free(path);

    out->llm_provider = arena_strdup(arena, raw.llm_provider ? raw.llm_provider : "anthropic");
    out->llm_model = arena_strdup(arena, raw.llm_model ? raw.llm_model : "claude-haiku-5-5");
    out->llm_base_url = arena_strdup(arena, raw.llm_base_url ? raw.llm_base_url : "");
    out->steam_language = arena_strdup(arena, raw.steam_language ? raw.steam_language : "english");
    out->steam_review_count = raw.steam_review_count ? atoi(raw.steam_review_count) : 200;
    out->steam_cache_ttl_hours = raw.steam_cache_ttl_hours ? atoi(raw.steam_cache_ttl_hours) : 24;
    out->review_default_length = arena_strdup(arena, raw.review_default_length ? raw.review_default_length : "short");
    out->review_default_tone = arena_strdup(arena, raw.review_default_tone ? raw.review_default_tone : "casual");

    free_raw_config(&raw);

    const char *anthropic_key = getenv("ANTHROPIC_API_KEY");
    const char *openai_key = getenv("OPENAI_API_KEY");
    out->anthropic_api_key = arena_strdup(arena, anthropic_key ? anthropic_key : "");
    out->openai_api_key = arena_strdup(arena, openai_key ? openai_key : "");

    out->verbose = false;

    return 0;
}

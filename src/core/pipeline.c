#include "core/pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/http.h"
#include "steam/cache.h"
#include "util/debug_log.h"
#include "util/strbuf.h"

int pipeline_resolve_llm(const config_t *cfg, const llm_provider_t **out_provider, llm_cfg_t *out_cfg, err_t *err)
{
    const llm_provider_t *provider = llm_provider_find(cfg->llm_provider);
    if (!provider)
    {
        err_set(err, -1, "unknown llm provider \"%s\"", cfg->llm_provider);
        return -1;
    }

    bool is_anthropic = strcmp(cfg->llm_provider, "anthropic") == 0;
    bool is_openai = strcmp(cfg->llm_provider, "openai") == 0;

    out_cfg->model = cfg->llm_model;
    out_cfg->base_url = cfg->llm_base_url;
    out_cfg->num_ctx = cfg->llm_num_ctx;
    out_cfg->api_key = is_anthropic ? cfg->anthropic_api_key : (is_openai ? cfg->openai_api_key : "");

    /* Fail fast with a precise message instead of letting an empty key
     * reach the HTTP layer, where it'd only ever show up as a generic
     * "HTTP 401" once the request round-trips. Anthropic always needs a
     * key; openai only needs one against the real API -- a base_url
     * pointing at a local server (LM Studio, llama-server, ...) is the
     * documented no-key case, so an empty key there is left alone. */
    if (is_anthropic && out_cfg->api_key[0] == '\0')
    {
        err_set(err, -1, "no Anthropic API key set -- export ANTHROPIC_API_KEY, or switch providers in Settings (F2) / config.ini");
        return -1;
    }
    if (is_openai && out_cfg->api_key[0] == '\0' && out_cfg->base_url[0] == '\0')
    {
        err_set(err, -1, "no OpenAI API key set -- export OPENAI_API_KEY, or switch providers in Settings (F2) / config.ini");
        return -1;
    }

    *out_provider = provider;
    return 0;
}

int pipeline_fetch_game(arena_t *arena, const config_t *cfg, uint32_t appid, game_info_t *out, err_t *err)
{
    char *cached = NULL;
    int hit = cache_get(arena, appid, "details", cfg->steam_cache_ttl_hours, &cached, err);

    if (hit == 1)
    {
        debug_logf("appdetails: cache hit for appid=%u", appid);
        return steam_parse_app_details(arena, appid, cached, out, err);
    }

    char url[256];
    snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%u&l=%s", appid, cfg->steam_language);

    debug_logf("appdetails: GET %s", url);

    http_cfg_t http_cfg = http_cfg_default();
    strbuf_t body;
    strbuf_init(&body);

    int rc = http_get(&http_cfg, url, &body, err);
    if (rc == 0)
    {
        rc = steam_parse_app_details(arena, appid, strbuf_cstr(&body), out, err);
        if (rc == 0) cache_put(appid, "details", strbuf_cstr(&body), err);
    }

    strbuf_free(&body);
    return rc;
}

int pipeline_fetch_themes(arena_t *arena, const config_t *cfg, uint32_t appid, const game_info_t *game, theme_set_t *out, err_t *err)
{
    char *cached = NULL;
    int hit = cache_get(arena, appid, "themes", cfg->steam_cache_ttl_hours, &cached, err);

    if (hit == 1)
    {
        debug_logf("themes: cache hit for appid=%u", appid);
        return themes_parse_response(arena, cached, out, err);
    }

    debug_logf("themes: sampling %d reviews for appid=%u", cfg->steam_review_count, appid);

    review_set_t reviews;
    if (steam_reviews_sample(arena, appid, cfg->steam_language, (size_t)cfg->steam_review_count, &reviews, err) != 0) return -1;

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    if (pipeline_resolve_llm(cfg, &provider, &llm_cfg, err) != 0) return -1;

    debug_logf("themes: calling provider=%s model=%s base_url=%s api_key=%s", provider->name, cfg->llm_model, cfg->llm_base_url[0] ? cfg->llm_base_url : "(default)", debug_log_redact_key(llm_cfg.api_key));

    if (themes_extract(arena, provider, &llm_cfg, game, &reviews, out, err) != 0) return -1;

    char *themes_json = themes_to_json(out);
    if (themes_json)
    {
        cache_put(appid, "themes", themes_json, err);
        free(themes_json);
    }

    return 0;
}

void pipeline_fetch_playtime(const config_t *cfg, uint32_t appid, steam_playtime_t *out)
{
    out->found = false;
    out->playtime_minutes = 0;

    if (!cfg->steam_api_key[0] || !cfg->steam_id[0])
    {
        debug_logf("playtime: skipped (steam_api_key or steam_id not set)");
        return;
    }

    err_t err;
    err_clear(&err);

    if (steam_fetch_playtime(cfg->steam_api_key, cfg->steam_id, appid, out, &err) != 0)
    {
        /* Deliberately not logging err.msg here: unlike every other API
         * this app calls, GetOwnedGames authenticates via a `key=...` query
         * parameter rather than a header, so err.msg (built from the full
         * request URL) would leak the raw Steam API key into debug.log. */
        debug_logf("playtime: request failed (code %d), skipping", err.code);
        out->found = false;
        out->playtime_minutes = 0;
        return;
    }

    debug_logf("playtime: found=%s minutes=%d", out->found ? "true" : "false", out->playtime_minutes);
}

int pipeline_fetch_library(arena_t *arena, const config_t *cfg, steam_library_t *out, err_t *err)
{
    if (!cfg->steam_api_key[0] || !cfg->steam_id[0])
    {
        err_set(err, -1, "no Steam ID/API key set -- add both in Settings (F2) to browse your library by playtime");
        return -1;
    }

    err_t steam_err;
    err_clear(&steam_err);

    if (steam_fetch_library(arena, cfg->steam_api_key, cfg->steam_id, out, &steam_err) != 0)
    {
        /* Deliberately not forwarding steam_err.msg: like
         * pipeline_fetch_playtime, it's built from the full request URL,
         * which would leak the raw Steam API key into whatever shows this
         * error (the TUI status bar, debug.log). */
        debug_logf("library: request failed (code %d)", steam_err.code);
        err_set(err, -1, "could not fetch your Steam library (code %d) -- check your Steam ID/API key in Settings (F2)", steam_err.code);
        return -1;
    }

    debug_logf("library: fetched %zu game(s)", out->count);
    return 0;
}

int pipeline_test_llm_connection(arena_t *arena, const config_t *cfg, const char *provider_name, const char *model, const char *base_url, int num_ctx, char **out_reply, err_t *err)
{
    config_t test_cfg = *cfg;
    test_cfg.llm_provider = (char *)provider_name;
    test_cfg.llm_model = (char *)model;
    test_cfg.llm_base_url = (char *)base_url;
    test_cfg.llm_num_ctx = num_ctx;

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    if (pipeline_resolve_llm(&test_cfg, &provider, &llm_cfg, err) != 0) return -1;

    strbuf_t response;
    strbuf_init(&response);

    int rc = provider->complete(&llm_cfg, "You are being used to test connectivity. Reply with just the word OK.", "Respond now.", &response, err);
    if (rc == 0) *out_reply = arena_strdup(arena, strbuf_cstr(&response));

    strbuf_free(&response);

    /* A successful test call just loaded the model (ollama) exactly like a
     * real review would -- unload it again so Ctrl+T doesn't leave it
     * sitting in VRAM/RAM until Ollama's own idle timeout just because you
     * were trying out a model, not actually generating a review. */
    if (rc == 0) pipeline_unload_llm_model(provider, &llm_cfg);

    return rc;
}

void pipeline_unload_llm_model(const llm_provider_t *provider, const llm_cfg_t *llm_cfg)
{
    if (!provider->unload) return;

    err_t err;
    err_clear(&err);

    if (provider->unload(llm_cfg, &err) != 0)
    {
        debug_logf("unload: failed to unload %s model \"%s\" (code %d), leaving it loaded", provider->name, llm_cfg->model, err.code);
        return;
    }

    debug_logf("unload: asked %s to unload model \"%s\"", provider->name, llm_cfg->model);
}

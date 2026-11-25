#include "core/pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/http.h"
#include "steam/cache.h"
#include "util/debug_log.h"

int pipeline_resolve_llm(const config_t *cfg, const llm_provider_t **out_provider, llm_cfg_t *out_cfg, err_t *err)
{
    const llm_provider_t *provider = llm_provider_find(cfg->llm_provider);
    if (!provider)
    {
        err_set(err, -1, "unknown llm provider \"%s\"", cfg->llm_provider);
        return -1;
    }

    bool is_anthropic = strcmp(cfg->llm_provider, "anthropic") == 0;
    out_cfg->model = cfg->llm_model;
    out_cfg->base_url = cfg->llm_base_url;
    out_cfg->api_key = is_anthropic ? cfg->anthropic_api_key : cfg->openai_api_key;

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

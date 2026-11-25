#define GOBLIN_ARGV_IMPLEMENTATION
#include <goblin/argv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "core/themes.h"
#include "llm/llm.h"
#include "net/http.h"
#include "steam/cache.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/debug_log.h"
#include "util/error.h"

#ifndef STEAMSCRIBE_VERSION
#define STEAMSCRIBE_VERSION "0.0.0"
#endif

static void print_version(void)
{
    printf("steamscribe %s\n", STEAMSCRIBE_VERSION);
}

static void print_help(void)
{
    printf("steamscribe %s\n", STEAMSCRIBE_VERSION);
    printf("Turn a game's Steam page and player reviews into a review draft you write yourself.\n\n");
    printf("Usage:\n");
    printf("  steamscribe                  interactive: asks for a game name\n");
    printf("  steamscribe <name>            start with a search for <name>\n");
    printf("  steamscribe --appid <id>       skip search, use an app id directly\n\n");
    printf("  steamscribe search <name>     print search matches\n");
    printf("  steamscribe info <appid>      print game info\n");
    printf("  steamscribe themes <appid>    print extracted likes/dislikes as JSON\n\n");
    printf("Options:\n");
    printf("  -h, --help                    show this help and exit\n");
    printf("      --version                show the version and exit\n");
    printf("      --verbose                log HTTP/LLM activity to debug.log\n");
}

static int cmd_search(arena_t *arena, const config_t *cfg, const char *query)
{
    steam_search_results_t results;
    err_t err;
    err_clear(&err);

    debug_logf("search: query=\"%s\" language=%s", query, cfg->steam_language);

    if (steam_search(arena, query, cfg->steam_language, &results, &err) != 0)
    {
        fprintf(stderr, "search failed: %s\n", err.msg);
        return 1;
    }

    if (results.count == 0)
    {
        printf("No matches for \"%s\"\n", query);
        return 0;
    }

    printf("Found %zu match(es) for \"%s\":\n", results.count, query);
    for (size_t i = 0; i < results.count; i++)
    {
        printf("  %-10u %s\n", results.items[i].appid, results.items[i].name);
    }

    return 0;
}

/* Fetches appdetails for `appid`, serving a fresh cache entry when there is
 * one and caching a fresh fetch otherwise. */
static int fetch_app_details_cached(arena_t *arena, const config_t *cfg, uint32_t appid, game_info_t *out, err_t *err)
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

static int cmd_info(arena_t *arena, const config_t *cfg, uint32_t appid)
{
    err_t err;
    err_clear(&err);

    game_info_t info;
    if (fetch_app_details_cached(arena, cfg, appid, &info, &err) != 0)
    {
        fprintf(stderr, "info failed: %s\n", err.msg);
        return 1;
    }

    review_set_t reviews;
    int reviews_rc = steam_reviews_summary(arena, appid, cfg->steam_language, &reviews, &err);

    printf("%s (%u)\n", info.name, info.appid);
    printf("Genres: %s\n", info.genres);
    printf("Developer: %s\n", info.developers);
    printf("Released: %s\n", info.release_date);
    printf("\n%s\n", info.short_desc);

    if (reviews_rc == 0)
    {
        printf("\nReviews: %s (%d positive / %d negative)\n", reviews.score_desc, reviews.total_positive, reviews.total_negative);
    }
    else
    {
        fprintf(stderr, "note: could not fetch review score: %s\n", err.msg);
    }

    return 0;
}

static int cmd_themes(arena_t *arena, const config_t *cfg, uint32_t appid)
{
    err_t err;
    err_clear(&err);

    char *cached = NULL;
    int hit = cache_get(arena, appid, "themes", cfg->steam_cache_ttl_hours, &cached, &err);

    theme_set_t themes;

    if (hit == 1)
    {
        debug_logf("themes: cache hit for appid=%u", appid);
        if (themes_parse_response(arena, cached, &themes, &err) != 0)
        {
            fprintf(stderr, "themes failed: %s\n", err.msg);
            return 1;
        }
    }
    else
    {
        game_info_t game;
        if (fetch_app_details_cached(arena, cfg, appid, &game, &err) != 0)
        {
            fprintf(stderr, "themes failed: %s\n", err.msg);
            return 1;
        }

        debug_logf("themes: sampling %d reviews for appid=%u", cfg->steam_review_count, appid);

        review_set_t reviews;
        if (steam_reviews_sample(arena, appid, cfg->steam_language, (size_t)cfg->steam_review_count, &reviews, &err) != 0)
        {
            fprintf(stderr, "themes failed: could not fetch reviews: %s\n", err.msg);
            return 1;
        }

        const llm_provider_t *provider = llm_provider_find(cfg->llm_provider);
        if (!provider)
        {
            fprintf(stderr, "themes failed: unknown llm provider \"%s\"\n", cfg->llm_provider);
            return 1;
        }

        bool is_anthropic = strcmp(cfg->llm_provider, "anthropic") == 0;
        llm_cfg_t llm_cfg;
        llm_cfg.model = cfg->llm_model;
        llm_cfg.base_url = cfg->llm_base_url;
        llm_cfg.api_key = is_anthropic ? cfg->anthropic_api_key : cfg->openai_api_key;

        debug_logf("themes: calling provider=%s model=%s base_url=%s api_key=%s", provider->name, cfg->llm_model, cfg->llm_base_url[0] ? cfg->llm_base_url : "(default)", debug_log_redact_key(llm_cfg.api_key));

        if (themes_extract(arena, provider, &llm_cfg, &game, &reviews, &themes, &err) != 0)
        {
            fprintf(stderr, "themes failed: %s\n", err.msg);
            return 1;
        }

        char *themes_json = themes_to_json(&themes);
        if (themes_json)
        {
            cache_put(appid, "themes", themes_json, &err);
            free(themes_json);
        }
    }

    char *output = themes_to_json(&themes);
    if (!output)
    {
        fprintf(stderr, "themes failed: out of memory\n");
        return 1;
    }

    printf("%s\n", output);
    free(output);
    return 0;
}

int main(int argc, char **argv)
{
    if (goblin_argv_has_flag(argc, argv, "--version"))
    {
        print_version();
        return 0;
    }

    if (goblin_argv_has_flag(argc, argv, "--help") || goblin_argv_has_flag(argc, argv, "-h"))
    {
        print_help();
        return 0;
    }

    if (goblin_argv_has_flag(argc, argv, "--verbose")) debug_log_enable();

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    err_clear(&err);
    if (config_load(&arena, &cfg, &err) != 0)
    {
        fprintf(stderr, "could not load config: %s\n", err.msg);
        arena_free(&arena);
        return 1;
    }

    int rc = 0;

    /* The subcommand is the first non-flag argument, wherever a flag like
     * --verbose happens to sit relative to it. */
    int cmd_i = -1;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            cmd_i = i;
            break;
        }
    }

    bool have_arg = cmd_i > 0 && cmd_i + 1 < argc;

    if (have_arg && strcmp(argv[cmd_i], "search") == 0)
    {
        rc = cmd_search(&arena, &cfg, argv[cmd_i + 1]);
    }
    else if (have_arg && strcmp(argv[cmd_i], "info") == 0)
    {
        uint32_t appid = (uint32_t)strtoul(argv[cmd_i + 1], NULL, 10);
        if (appid == 0)
        {
            fprintf(stderr, "info: \"%s\" is not a valid app id\n", argv[cmd_i + 1]);
            rc = 1;
        }
        else
        {
            rc = cmd_info(&arena, &cfg, appid);
        }
    }
    else if (have_arg && strcmp(argv[cmd_i], "themes") == 0)
    {
        uint32_t appid = (uint32_t)strtoul(argv[cmd_i + 1], NULL, 10);
        if (appid == 0)
        {
            fprintf(stderr, "themes: \"%s\" is not a valid app id\n", argv[cmd_i + 1]);
            rc = 1;
        }
        else
        {
            rc = cmd_themes(&arena, &cfg, appid);
        }
    }
    else
    {
        print_help();
    }

    arena_free(&arena);
    return rc;
}

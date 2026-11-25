#define GOBLIN_ARGV_IMPLEMENTATION
#include <goblin/argv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/http.h"
#include "steam/cache.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

#ifndef STEAMSCRIBE_VERSION
#define STEAMSCRIBE_VERSION "0.0.0"
#endif

#define DEFAULT_LANGUAGE "english"

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
}

static int cmd_search(arena_t *arena, const char *query)
{
    steam_search_results_t results;
    err_t err;
    err_clear(&err);

    if (steam_search(arena, query, DEFAULT_LANGUAGE, &results, &err) != 0)
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

static int cmd_info(arena_t *arena, uint32_t appid)
{
    err_t err;
    err_clear(&err);

    char *cached = NULL;
    int hit = cache_get(arena, appid, "details", CACHE_TTL_HOURS_DEFAULT, &cached, &err);

    game_info_t info;
    int rc;

    if (hit == 1)
    {
        rc = steam_parse_app_details(arena, appid, cached, &info, &err);
    }
    else
    {
        char url[256];
        snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%u&l=%s", appid, DEFAULT_LANGUAGE);

        http_cfg_t cfg = http_cfg_default();
        strbuf_t body;
        strbuf_init(&body);

        rc = http_get(&cfg, url, &body, &err);
        if (rc == 0)
        {
            rc = steam_parse_app_details(arena, appid, strbuf_cstr(&body), &info, &err);
            if (rc == 0) cache_put(appid, "details", strbuf_cstr(&body), &err);
        }

        strbuf_free(&body);
    }

    if (rc != 0)
    {
        fprintf(stderr, "info failed: %s\n", err.msg);
        return 1;
    }

    review_set_t reviews;
    int reviews_rc = steam_reviews_summary(arena, appid, DEFAULT_LANGUAGE, &reviews, &err);

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

    if (argc >= 3 && strcmp(argv[1], "search") == 0)
    {
        arena_t arena;
        arena_init(&arena, 0);
        int rc = cmd_search(&arena, argv[2]);
        arena_free(&arena);
        return rc;
    }

    if (argc >= 3 && strcmp(argv[1], "info") == 0)
    {
        uint32_t appid = (uint32_t)strtoul(argv[2], NULL, 10);
        if (appid == 0)
        {
            fprintf(stderr, "info: \"%s\" is not a valid app id\n", argv[2]);
            return 1;
        }

        arena_t arena;
        arena_init(&arena, 0);
        int rc = cmd_info(&arena, appid);
        arena_free(&arena);
        return rc;
    }

    print_help();
    return 0;
}

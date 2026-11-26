#ifndef STEAMSCRIBE_STEAM_STEAM_H
#define STEAMSCRIBE_STEAM_STEAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/arena.h"
#include "util/error.h"

typedef struct steam_search_result_t
{
    uint32_t appid;
    char *name;
} steam_search_result_t;

typedef struct steam_search_results_t
{
    steam_search_result_t *items;
    size_t count;
} steam_search_results_t;

typedef struct game_info_t
{
    uint32_t appid;
    char *name;
    char *short_desc;
    char *genres;      /* comma-joined */
    char *developers;  /* comma-joined */
    char *release_date;
} game_info_t;

typedef struct steam_review_t
{
    char *text;
    bool voted_up;
    int votes_up;
    int playtime_min;
} steam_review_t;

typedef struct review_set_t
{
    steam_review_t *items;
    size_t count;
    char *score_desc;
    int total_positive;
    int total_negative;
} review_set_t;

/* One page of IUserReviewsService/GetAppReviews, as returned for a single
 * review_type. The score fields (score_desc/total_positive/total_negative)
 * are only present on the first page when review_type is "all". */
typedef struct steam_reviews_page_t
{
    char *cursor;
    steam_review_t *items;
    size_t count;
    bool has_summary;
    char *score_desc;
    int total_positive;
    int total_negative;
} steam_reviews_page_t;

/* From IPlayerService/GetOwnedGames, filtered to a single appid.
 * `found` is false if that appid isn't in the account's owned-games list
 * at all (never purchased/played, or the profile/game details are
 * private) -- not an error, just nothing to report. */
typedef struct steam_playtime_t
{
    bool found;
    int playtime_minutes; /* playtime_forever; valid only if found */
} steam_playtime_t;

/* Pure parsers: no network, safe to feed fixture JSON in tests. Results
 * (and any strings in them) are allocated from `arena`. */
int steam_parse_search(arena_t *arena, const char *json_text, steam_search_results_t *out, err_t *err);
int steam_parse_app_details(arena_t *arena, uint32_t appid, const char *json_text, game_info_t *out, err_t *err);
int steam_parse_reviews_page(arena_t *arena, const char *json_text, steam_reviews_page_t *out, err_t *err);
int steam_parse_owned_games(const char *json_text, steam_playtime_t *out, err_t *err);

/* Network calls. `language` is a Steam store language code, e.g. "english". */
int steam_search(arena_t *arena, const char *query, const char *language, steam_search_results_t *out, err_t *err);
int steam_app_details(arena_t *arena, uint32_t appid, const char *language, game_info_t *out, err_t *err);

/* Just the score/tally, from a single cheap request (no review text). */
int steam_reviews_summary(arena_t *arena, uint32_t appid, const char *language, review_set_t *out, err_t *err);

/* Up to `count` reviews split evenly between positive/negative, cursor-paginated,
 * with very short reviews dropped and long ones clipped. */
int steam_reviews_sample(arena_t *arena, uint32_t appid, const char *language, size_t count, review_set_t *out, err_t *err);

/* Steam Web API, not the public store API the rest of this file uses --
 * needs the account's own api_key and steam_id (SteamID64). */
int steam_fetch_playtime(const char *api_key, const char *steam_id, uint32_t appid, steam_playtime_t *out, err_t *err);

#endif

#include "test_assert.h"

#include "core/themes.h"
#include "llm/fake.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static review_set_t make_mixed_reviews(arena_t *arena)
{
    review_set_t reviews;
    reviews.count = 2;
    reviews.items = (steam_review_t *)arena_alloc(arena, 2 * sizeof(steam_review_t));
    reviews.items[0].text = arena_strdup(arena, "The building system is great and co-op with friends is a blast.");
    reviews.items[0].voted_up = true;
    reviews.items[0].votes_up = 10;
    reviews.items[0].playtime_min = 500;
    reviews.items[1].text = arena_strdup(arena, "Enemy raids destroy my base constantly, very annoying.");
    reviews.items[1].voted_up = false;
    reviews.items[1].votes_up = 3;
    reviews.items[1].playtime_min = 200;
    reviews.score_desc = arena_strdup(arena, "Very Positive");
    reviews.total_positive = 100;
    reviews.total_negative = 10;
    return reviews;
}

static game_info_t make_game(arena_t *arena)
{
    game_info_t game;
    game.appid = 892970;
    game.name = arena_strdup(arena, "Valheim");
    game.short_desc = arena_strdup(arena, "A survival game.");
    game.genres = arena_strdup(arena, "Action, Adventure");
    game.developers = arena_strdup(arena, "Iron Gate AB");
    game.release_date = arena_strdup(arena, "9 Sep, 2026");
    return game;
}

static llm_cfg_t make_fake_cfg(void)
{
    llm_cfg_t cfg;
    cfg.model = "fake-model";
    cfg.base_url = "";
    cfg.api_key = "";
    cfg.num_ctx = 0;
    return cfg;
}

static int test_build_side_prompt_liked(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_mixed_reviews(&arena);

    char *system = NULL;
    char *user = NULL;
    themes_build_side_prompt(&arena, &game, &reviews, true, &system, &user);

    TEST_ASSERT(strstr(system, "LIKED") != NULL);
    TEST_ASSERT(strstr(system, "\"liked\"") != NULL);
    TEST_ASSERT(strstr(system, "\"disliked\"") == NULL);
    TEST_ASSERT(strstr(user, "Positive reviews:") != NULL);
    TEST_ASSERT(strstr(user, "building system is great") != NULL);
    TEST_ASSERT(strstr(user, "Enemy raids") == NULL); /* negative review excluded from the liked-side prompt */

    arena_free(&arena);
    return 0;
}

static int test_build_side_prompt_disliked_empty(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);

    review_set_t all_positive;
    all_positive.count = 1;
    all_positive.items = (steam_review_t *)arena_alloc(&arena, sizeof(steam_review_t));
    all_positive.items[0].text = arena_strdup(&arena, "Loved every minute.");
    all_positive.items[0].voted_up = true;
    all_positive.items[0].votes_up = 1;
    all_positive.items[0].playtime_min = 10;

    char *system = NULL;
    char *user = NULL;
    themes_build_side_prompt(&arena, &game, &all_positive, false, &system, &user);

    TEST_ASSERT(strstr(system, "DISLIKED") != NULL);
    TEST_ASSERT(strstr(user, "Negative reviews:") != NULL);
    TEST_ASSERT(strstr(user, "(none)") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_parse_side_response_plain(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_t *items = NULL;
    size_t count = 0;
    err_t err;
    int rc = themes_parse_side_response(&arena, "{\"liked\":[{\"id\":\"co_op\",\"label\":\"Co-op with friends\"}]}", true, &items, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(items[0].id, "co_op") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_side_response_fenced(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_t *items = NULL;
    size_t count = 0;
    err_t err;
    int rc = themes_parse_side_response(&arena, "```json\n{\"disliked\":[{\"id\":\"raids\",\"label\":\"Enemy raids\"}]}\n```", false, &items, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(items[0].label, "Enemy raids") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_side_response_wrong_key(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_t *items = NULL;
    size_t count = 0;
    err_t err;
    /* asking for "liked" but the response only has "disliked" */
    int rc = themes_parse_side_response(&arena, "{\"disliked\":[]}", true, &items, &count, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_side_response_invalid_json(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_t *items = NULL;
    size_t count = 0;
    err_t err;
    int rc = themes_parse_side_response(&arena, "not json at all", true, &items, &count, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_cache_round_trip(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    const char *text = "{\"liked\":[{\"id\":\"co_op\",\"label\":\"Co-op with friends\"}],\"disliked\":[{\"id\":\"raids\",\"label\":\"Enemy raids\"}]}";

    theme_set_t themes;
    err_t err;
    TEST_ASSERT(themes_parse_response(&arena, text, &themes, &err) == 0);

    char *json = themes_to_json(&themes);
    TEST_ASSERT(json != NULL);

    theme_set_t round_tripped;
    TEST_ASSERT(themes_parse_response(&arena, json, &round_tripped, &err) == 0);
    free(json);

    TEST_ASSERT(round_tripped.n_liked == 1);
    TEST_ASSERT(strcmp(round_tripped.liked[0].id, "co_op") == 0);
    TEST_ASSERT(round_tripped.n_disliked == 1);
    TEST_ASSERT(strcmp(round_tripped.disliked[0].label, "Enemy raids") == 0);

    arena_free(&arena);
    return 0;
}

static int test_extract_two_pass_merges_sides(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_mixed_reviews(&arena);
    llm_cfg_t cfg = make_fake_cfg();

    const char *responses[2] = {
        "{\"liked\":[{\"id\":\"co_op\",\"label\":\"Co-op with friends\"}]}",
        "{\"disliked\":[{\"id\":\"raids\",\"label\":\"Enemy raids\"}]}",
    };
    llm_fake_set_response_sequence(responses, 2);

    theme_set_t themes;
    err_t err;
    int rc = themes_extract(&arena, &llm_provider_fake, &cfg, &game, &reviews, &themes, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(themes.n_liked == 1);
    TEST_ASSERT(strcmp(themes.liked[0].id, "co_op") == 0);
    TEST_ASSERT(themes.n_disliked == 1);
    TEST_ASSERT(strcmp(themes.disliked[0].id, "raids") == 0);

    arena_free(&arena);
    return 0;
}

static int test_extract_retries_within_a_side_then_succeeds(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_mixed_reviews(&arena);
    llm_cfg_t cfg = make_fake_cfg();

    /* call 1 (liked, attempt 1): invalid -> retried
     * call 2 (liked, attempt 2 / the retry): valid
     * call 3 (disliked, attempt 1): valid */
    const char *responses[3] = {
        "not valid json",
        "{\"liked\":[{\"id\":\"co_op\",\"label\":\"Co-op with friends\"}]}",
        "{\"disliked\":[{\"id\":\"raids\",\"label\":\"Enemy raids\"}]}",
    };
    llm_fake_set_response_sequence(responses, 3);

    theme_set_t themes;
    err_t err;
    int rc = themes_extract(&arena, &llm_provider_fake, &cfg, &game, &reviews, &themes, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(themes.n_liked == 1);
    TEST_ASSERT(themes.n_disliked == 1);

    arena_free(&arena);
    return 0;
}

static int test_add_custom_item_to_empty_set(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_t *items = NULL;
    size_t count = 0;

    themes_add_custom_item(&arena, &items, &count, "Great soundtrack");

    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(items[0].label, "Great soundtrack") == 0);
    TEST_ASSERT(items[0].checked); /* a custom item is checked by default */
    TEST_ASSERT(items[0].id != NULL && items[0].id[0] != '\0');

    arena_free(&arena);
    return 0;
}

static int test_add_custom_item_appends_and_keeps_existing(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_t *items = (theme_t *)arena_alloc(&arena, 2 * sizeof(theme_t));
    items[0].id = arena_strdup(&arena, "building");
    items[0].label = arena_strdup(&arena, "Building system");
    items[0].checked = true;
    items[1].id = arena_strdup(&arena, "co_op");
    items[1].label = arena_strdup(&arena, "Co-op with friends");
    items[1].checked = false;
    size_t count = 2;

    themes_add_custom_item(&arena, &items, &count, "Boss fights");

    TEST_ASSERT(count == 3);
    TEST_ASSERT(strcmp(items[0].label, "Building system") == 0);
    TEST_ASSERT(items[0].checked);
    TEST_ASSERT(strcmp(items[1].label, "Co-op with friends") == 0);
    TEST_ASSERT(!items[1].checked);
    TEST_ASSERT(strcmp(items[2].label, "Boss fights") == 0);
    TEST_ASSERT(items[2].checked);

    arena_free(&arena);
    return 0;
}

static int test_extract_fails_after_retry_exhausted(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_mixed_reviews(&arena);
    llm_cfg_t cfg = make_fake_cfg();

    llm_fake_set_response("still not json");

    theme_set_t themes;
    err_t err;
    int rc = themes_extract(&arena, &llm_provider_fake, &cfg, &game, &reviews, &themes, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_build_side_prompt_liked()) return 1;
    if (test_build_side_prompt_disliked_empty()) return 1;
    if (test_parse_side_response_plain()) return 1;
    if (test_parse_side_response_fenced()) return 1;
    if (test_parse_side_response_wrong_key()) return 1;
    if (test_parse_side_response_invalid_json()) return 1;
    if (test_parse_response_cache_round_trip()) return 1;
    if (test_extract_two_pass_merges_sides()) return 1;
    if (test_extract_retries_within_a_side_then_succeeds()) return 1;
    if (test_extract_fails_after_retry_exhausted()) return 1;
    if (test_add_custom_item_to_empty_set()) return 1;
    if (test_add_custom_item_appends_and_keeps_existing()) return 1;

    printf("all tests passed\n");
    return 0;
}

#include "test_assert.h"

#include "core/themes.h"
#include "llm/fake.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static review_set_t make_reviews(arena_t *arena)
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
    return cfg;
}

static int test_build_prompt(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_reviews(&arena);

    char *system = NULL;
    char *user = NULL;
    themes_build_prompt(&arena, &game, &reviews, &system, &user);

    TEST_ASSERT(strstr(system, "JSON only") != NULL);
    TEST_ASSERT(strstr(user, "Valheim") != NULL);
    TEST_ASSERT(strstr(user, "building system is great") != NULL);
    TEST_ASSERT(strstr(user, "Enemy raids") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_parse_plain_json(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    const char *text = "{\"liked\":[{\"id\":\"building\",\"label\":\"Building system\"}],\"disliked\":[{\"id\":\"raids\",\"label\":\"Enemy raids\"}]}";

    theme_set_t themes;
    err_t err;
    int rc = themes_parse_response(&arena, text, &themes, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(themes.n_liked == 1);
    TEST_ASSERT(strcmp(themes.liked[0].id, "building") == 0);
    TEST_ASSERT(themes.n_disliked == 1);
    TEST_ASSERT(strcmp(themes.disliked[0].label, "Enemy raids") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_fenced_json(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    const char *text = "```json\n{\"liked\":[{\"id\":\"a\",\"label\":\"A\"}],\"disliked\":[]}\n```";

    theme_set_t themes;
    err_t err;
    int rc = themes_parse_response(&arena, text, &themes, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(themes.n_liked == 1);
    TEST_ASSERT(themes.n_disliked == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_invalid_json(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    theme_set_t themes;
    err_t err;
    int rc = themes_parse_response(&arena, "not json at all", &themes, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_extract_success(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_reviews(&arena);
    llm_cfg_t cfg = make_fake_cfg();

    llm_fake_set_response("{\"liked\":[{\"id\":\"co_op\",\"label\":\"Co-op with friends\"}],\"disliked\":[{\"id\":\"raids\",\"label\":\"Enemy raids\"}]}");

    theme_set_t themes;
    err_t err;
    int rc = themes_extract(&arena, &llm_provider_fake, &cfg, &game, &reviews, &themes, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(themes.n_liked == 1);
    TEST_ASSERT(strcmp(themes.liked[0].id, "co_op") == 0);

    arena_free(&arena);
    return 0;
}

static int test_extract_retries_once_then_succeeds(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_reviews(&arena);
    llm_cfg_t cfg = make_fake_cfg();

    llm_fake_set_sequence("not valid json", "{\"liked\":[],\"disliked\":[{\"id\":\"grind\",\"label\":\"Grindy progression\"}]}");

    theme_set_t themes;
    err_t err;
    int rc = themes_extract(&arena, &llm_provider_fake, &cfg, &game, &reviews, &themes, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(themes.n_disliked == 1);
    TEST_ASSERT(strcmp(themes.disliked[0].id, "grind") == 0);

    arena_free(&arena);
    return 0;
}

static int test_extract_fails_after_retry(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    review_set_t reviews = make_reviews(&arena);
    llm_cfg_t cfg = make_fake_cfg();

    llm_fake_set_response("still not json");

    theme_set_t themes;
    err_t err;
    int rc = themes_extract(&arena, &llm_provider_fake, &cfg, &game, &reviews, &themes, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_to_json_round_trips(void)
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

int main(void)
{
    if (test_build_prompt()) return 1;
    if (test_parse_plain_json()) return 1;
    if (test_parse_fenced_json()) return 1;
    if (test_parse_invalid_json()) return 1;
    if (test_extract_success()) return 1;
    if (test_extract_retries_once_then_succeeds()) return 1;
    if (test_extract_fails_after_retry()) return 1;
    if (test_to_json_round_trips()) return 1;

    printf("all tests passed\n");
    return 0;
}

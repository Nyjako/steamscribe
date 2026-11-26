#include "test_assert.h"

#include "core/followup.h"
#include "llm/fake.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <string.h>

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

static theme_t make_theme(arena_t *arena, const char *id, const char *label, bool checked)
{
    theme_t t;
    t.id = arena_strdup(arena, id);
    t.label = arena_strdup(arena, label);
    t.checked = checked;
    return t;
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

static int test_build_prompt_uses_only_checked_items(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);

    theme_t liked[2];
    liked[0] = make_theme(&arena, "building", "Building system", true);
    liked[1] = make_theme(&arena, "sailing", "Sailing", false);

    theme_t disliked[1];
    disliked[0] = make_theme(&arena, "raids", "Enemy raids", true);

    theme_set_t themes;
    themes.liked = liked;
    themes.n_liked = 2;
    themes.disliked = disliked;
    themes.n_disliked = 1;

    char *system = NULL;
    char *user = NULL;
    followup_build_prompt(&arena, &game, &themes, &system, &user);

    TEST_ASSERT(strstr(system, "\"questions\"") != NULL);
    TEST_ASSERT(strstr(user, "Valheim") != NULL);
    TEST_ASSERT(strstr(user, "Building system") != NULL);
    TEST_ASSERT(strstr(user, "Sailing") == NULL); /* not checked, excluded */
    TEST_ASSERT(strstr(user, "Enemy raids") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_build_prompt_empty_themes_says_nothing_specific(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);

    theme_set_t themes;
    themes.liked = NULL;
    themes.n_liked = 0;
    themes.disliked = NULL;
    themes.n_disliked = 0;

    char *system = NULL;
    char *user = NULL;
    followup_build_prompt(&arena, &game, &themes, &system, &user);

    TEST_ASSERT(strstr(user, "(nothing specific)") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_plain(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_parse_response(&arena, "{\"questions\":[\"What made you keep playing?\",\"Any moment that stuck with you?\"]}", &questions, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == 2);
    TEST_ASSERT(strcmp(questions[0], "What made you keep playing?") == 0);
    TEST_ASSERT(strcmp(questions[1], "Any moment that stuck with you?") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_fenced(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_parse_response(&arena, "```json\n{\"questions\":[\"Why did you keep coming back?\"]}\n```", &questions, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(questions[0], "Why did you keep coming back?") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_wrong_key(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_parse_response(&arena, "{\"liked\":[]}", &questions, &count, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_invalid_json(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_parse_response(&arena, "not json at all", &questions, &count, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_clamps_to_max(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_parse_response(&arena, "{\"questions\":[\"q1?\",\"q2?\",\"q3?\",\"q4?\",\"q5?\"]}", &questions, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == FOLLOWUP_MAX_QUESTIONS);

    arena_free(&arena);
    return 0;
}

static int test_generate_uses_fake_provider(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    theme_set_t themes;
    themes.liked = NULL;
    themes.n_liked = 0;
    themes.disliked = NULL;
    themes.n_disliked = 0;
    llm_cfg_t cfg = make_fake_cfg();

    llm_fake_set_response("{\"questions\":[\"What surprised you most?\"]}");

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_generate(&arena, &llm_provider_fake, &cfg, &game, &themes, &questions, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(questions[0], "What surprised you most?") == 0);

    arena_free(&arena);
    return 0;
}

static int test_generate_retries_then_succeeds(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    theme_set_t themes;
    themes.liked = NULL;
    themes.n_liked = 0;
    themes.disliked = NULL;
    themes.n_disliked = 0;
    llm_cfg_t cfg = make_fake_cfg();

    const char *responses[2] = {
        "not valid json",
        "{\"questions\":[\"What kept you up late playing?\"]}",
    };
    llm_fake_set_response_sequence(responses, 2);

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_generate(&arena, &llm_provider_fake, &cfg, &game, &themes, &questions, &count, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(count == 1);

    arena_free(&arena);
    return 0;
}

static int test_generate_fails_after_retry_exhausted(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game = make_game(&arena);
    theme_set_t themes;
    themes.liked = NULL;
    themes.n_liked = 0;
    themes.disliked = NULL;
    themes.n_disliked = 0;
    llm_cfg_t cfg = make_fake_cfg();

    llm_fake_set_response("still not json");

    char **questions = NULL;
    size_t count = 0;
    err_t err;
    int rc = followup_generate(&arena, &llm_provider_fake, &cfg, &game, &themes, &questions, &count, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_build_prompt_uses_only_checked_items()) return 1;
    if (test_build_prompt_empty_themes_says_nothing_specific()) return 1;
    if (test_parse_response_plain()) return 1;
    if (test_parse_response_fenced()) return 1;
    if (test_parse_response_wrong_key()) return 1;
    if (test_parse_response_invalid_json()) return 1;
    if (test_parse_response_clamps_to_max()) return 1;
    if (test_generate_uses_fake_provider()) return 1;
    if (test_generate_retries_then_succeeds()) return 1;
    if (test_generate_fails_after_retry_exhausted()) return 1;

    printf("all tests passed\n");
    return 0;
}

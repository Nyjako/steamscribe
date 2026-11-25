#include "test_assert.h"

#include "core/review.h"
#include "llm/fake.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <string.h>

static theme_t make_theme(arena_t *arena, const char *id, const char *label, bool checked)
{
    theme_t t;
    t.id = arena_strdup(arena, id);
    t.label = arena_strdup(arena, label);
    t.checked = checked;
    return t;
}

static int test_full_prompt_golden(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game;
    game.appid = 892970;
    game.name = arena_strdup(&arena, "Valheim");
    game.short_desc = arena_strdup(&arena, "A survival game.");
    game.genres = arena_strdup(&arena, "Action, Adventure");
    game.developers = arena_strdup(&arena, "Iron Gate AB");
    game.release_date = arena_strdup(&arena, "9 Sep, 2026");

    theme_t liked[3];
    liked[0] = make_theme(&arena, "building", "Building system", true);
    liked[1] = make_theme(&arena, "sailing", "Sailing", false);
    liked[2] = make_theme(&arena, "co_op", "Co-op with friends", true);

    theme_t disliked[2];
    disliked[0] = make_theme(&arena, "raids", "Enemy raids", true);
    disliked[1] = make_theme(&arena, "grind", "Grindy progression", false);

    theme_set_t themes;
    themes.liked = liked;
    themes.n_liked = 3;
    themes.disliked = disliked;
    themes.n_disliked = 2;

    review_request_t req;
    req.game = &game;
    req.themes = &themes;
    req.notes = "great with friends";
    req.recommend = true;
    req.length = REVIEW_LEN_MEDIUM;
    req.tone = REVIEW_TONE_ENTHUSIASTIC;
    req.vocab = REVIEW_VOCAB_NORMAL;

    char *system = NULL;
    char *user = NULL;
    review_build_prompt(&arena, &req, &system, &user);

    const char *expected_system =
        "You write a short, honest Steam review in the voice of the player themselves, using only the points they give you. Follow these rules exactly:\n"
        "- Write in first person, as the player.\n"
        "- Use only the provided points below. Never invent or add any extra like, dislike, experience, number, or feature -- not even ones that sound typical for this kind of game.\n"
        "- Plain text only: no headings, no markdown, no emojis unless told otherwise below.\n"
        "- End with a single-line verdict stating whether you recommend the game.\n"
        "- Length: 1 to 2 paragraphs.\n"
        "- Write with enthusiasm and energy; the occasional emoji is fine.\n";

    const char *expected_user =
        "Game: Valheim\n"
        "\nWhat I liked:\n"
        "- Building system\n"
        "- Co-op with friends\n"
        "\nWhat bothered me:\n"
        "- Enemy raids\n"
        "\nAdditional notes: great with friends\n"
        "\nOverall recommendation: yes\n";

    TEST_ASSERT(strcmp(system, expected_system) == 0);
    TEST_ASSERT(strcmp(user, expected_user) == 0);

    arena_free(&arena);
    return 0;
}

static int test_minimal_prompt_golden(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game;
    game.appid = 1;
    game.name = arena_strdup(&arena, "Puzzlegame");
    game.short_desc = arena_strdup(&arena, "A puzzle game.");
    game.genres = arena_strdup(&arena, "Puzzle");
    game.developers = arena_strdup(&arena, "Someone");
    game.release_date = arena_strdup(&arena, "1 Jan, 2026");

    theme_t liked[1];
    liked[0] = make_theme(&arena, "art", "Art style", false);

    theme_set_t themes;
    themes.liked = liked;
    themes.n_liked = 1;
    themes.disliked = NULL;
    themes.n_disliked = 0;

    review_request_t req;
    req.game = &game;
    req.themes = &themes;
    req.notes = "";
    req.recommend = false;
    req.length = REVIEW_LEN_SHORT;
    req.tone = REVIEW_TONE_CASUAL;
    req.vocab = REVIEW_VOCAB_NORMAL;

    char *system = NULL;
    char *user = NULL;
    review_build_prompt(&arena, &req, &system, &user);

    const char *expected_system =
        "You write a short, honest Steam review in the voice of the player themselves, using only the points they give you. Follow these rules exactly:\n"
        "- Write in first person, as the player.\n"
        "- Use only the provided points below. Never invent or add any extra like, dislike, experience, number, or feature -- not even ones that sound typical for this kind of game.\n"
        "- Plain text only: no headings, no markdown, no emojis unless told otherwise below.\n"
        "- End with a single-line verdict stating whether you recommend the game.\n"
        "- Length: 2 to 4 sentences.\n"
        "- Write casually, like you're telling a friend about it.\n";

    const char *expected_user =
        "Game: Puzzlegame\n"
        "\nWhat I liked:\n"
        "(nothing specific)\n"
        "\nWhat bothered me:\n"
        "(nothing specific)\n"
        "\nAdditional notes: (none)\n"
        "\nOverall recommendation: no\n";

    TEST_ASSERT(strcmp(system, expected_system) == 0);
    TEST_ASSERT(strcmp(user, expected_user) == 0);

    arena_free(&arena);
    return 0;
}

static int test_simple_vocab_adds_instruction_line(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game;
    game.appid = 1;
    game.name = arena_strdup(&arena, "Puzzlegame");
    game.short_desc = arena_strdup(&arena, "A puzzle game.");
    game.genres = arena_strdup(&arena, "Puzzle");
    game.developers = arena_strdup(&arena, "Someone");
    game.release_date = arena_strdup(&arena, "1 Jan, 2026");

    theme_set_t themes;
    themes.liked = NULL;
    themes.n_liked = 0;
    themes.disliked = NULL;
    themes.n_disliked = 0;

    review_request_t req;
    req.game = &game;
    req.themes = &themes;
    req.notes = "";
    req.recommend = true;
    req.length = REVIEW_LEN_SHORT;
    req.tone = REVIEW_TONE_CASUAL;
    req.vocab = REVIEW_VOCAB_SIMPLE;

    char *system = NULL;
    char *user = NULL;
    review_build_prompt(&arena, &req, &system, &user);

    TEST_ASSERT(strstr(system, "simple, everyday words") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_generate_uses_fake_provider(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    game_info_t game;
    game.appid = 892970;
    game.name = arena_strdup(&arena, "Valheim");
    game.short_desc = arena_strdup(&arena, "A survival game.");
    game.genres = arena_strdup(&arena, "Action");
    game.developers = arena_strdup(&arena, "Iron Gate AB");
    game.release_date = arena_strdup(&arena, "9 Sep, 2026");

    theme_set_t themes;
    themes.liked = NULL;
    themes.n_liked = 0;
    themes.disliked = NULL;
    themes.n_disliked = 0;

    review_request_t req;
    req.game = &game;
    req.themes = &themes;
    req.notes = "";
    req.recommend = true;
    req.length = REVIEW_LEN_SHORT;
    req.tone = REVIEW_TONE_CASUAL;
    req.vocab = REVIEW_VOCAB_NORMAL;

    llm_fake_set_response("I really liked it. Recommended!");

    llm_cfg_t cfg;
    cfg.model = "fake-model";
    cfg.base_url = "";
    cfg.api_key = "";
    cfg.num_ctx = 0;

    char *text = NULL;
    err_t err;
    int rc = review_generate(&arena, &llm_provider_fake, &cfg, &req, &text, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(text, "I really liked it. Recommended!") == 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_full_prompt_golden()) return 1;
    if (test_minimal_prompt_golden()) return 1;
    if (test_simple_vocab_adds_instruction_line()) return 1;
    if (test_generate_uses_fake_provider()) return 1;

    printf("all tests passed\n");
    return 0;
}

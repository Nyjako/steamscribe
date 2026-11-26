#include "test_assert.h"

#include "tui/keys.h"
#include "tui/state.h"
#include "util/arena.h"

#include <stdio.h>
#include <string.h>

static theme_t make_theme(arena_t *arena, const char *id, const char *label)
{
    theme_t t;
    t.id = arena_strdup(arena, id);
    t.label = arena_strdup(arena, label);
    t.checked = false;
    return t;
}

static config_t make_fake_config(arena_t *arena)
{
    config_t cfg;
    cfg.llm_provider = arena_strdup(arena, "anthropic");
    cfg.llm_model = arena_strdup(arena, "claude-haiku-5-5");
    cfg.llm_base_url = arena_strdup(arena, "");
    cfg.llm_num_ctx = 8192;
    cfg.anthropic_api_key = arena_strdup(arena, "");
    cfg.openai_api_key = arena_strdup(arena, "");
    cfg.steam_language = arena_strdup(arena, "english");
    cfg.steam_review_count = 200;
    cfg.steam_cache_ttl_hours = 24;
    cfg.steam_id = arena_strdup(arena, "");
    cfg.steam_api_key = arena_strdup(arena, "");
    cfg.steam_api_key_from_env = false;
    cfg.review_default_length = arena_strdup(arena, "short");
    cfg.review_default_tone = arena_strdup(arena, "casual");
    cfg.review_default_vocab = arena_strdup(arena, "normal");
    cfg.verbose = false;
    return cfg;
}

static int test_init_prefills_search(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "valheim", 0);

    TEST_ASSERT(state.screen == TUI_SCREEN_SEARCH);
    TEST_ASSERT(strcmp(state.search_input, "valheim") == 0);

    arena_free(&arena);
    return 0;
}

static int test_init_with_appid_skips_search(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_GAME);
    TEST_ASSERT(state.appid == 892970);

    arena_free(&arena);
    return 0;
}

static int test_search_typing_and_enter(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE); /* empty input: no-op */

    tui_handle_key(&state, 'v', 0);
    tui_handle_key(&state, 'a', 0);
    tui_handle_key(&state, 'l', 0);
    TEST_ASSERT(strcmp(state.search_input, "val") == 0);

    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(state.search_input, "va") == 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_SEARCH);

    arena_free(&arena);
    return 0;
}

static int test_apply_search_results_zero_one_many(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "x", 0);

    steam_search_results_t empty = {0};
    tui_apply_search_results(&state, empty);
    TEST_ASSERT(state.screen == TUI_SCREEN_SEARCH);
    TEST_ASSERT(strstr(state.status, "No matches") != NULL);

    steam_search_result_t one_item[1];
    one_item[0].appid = 892970;
    one_item[0].name = arena_strdup(&arena, "Valheim");
    steam_search_results_t one = {one_item, 1};
    tui_apply_search_results(&state, one);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_GAME);
    TEST_ASSERT(state.appid == 892970);

    steam_search_result_t many_items[2];
    many_items[0].appid = 1;
    many_items[0].name = arena_strdup(&arena, "A");
    many_items[1].appid = 2;
    many_items[1].name = arena_strdup(&arena, "B");
    steam_search_results_t many = {many_items, 2};
    tui_apply_search_results(&state, many);
    TEST_ASSERT(state.screen == TUI_SCREEN_SELECT);

    arena_free(&arena);
    return 0;
}

static int test_select_screen_navigation(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "x", 0);

    steam_search_result_t items[3];
    items[0].appid = 10;
    items[0].name = arena_strdup(&arena, "A");
    items[1].appid = 20;
    items[1].name = arena_strdup(&arena, "B");
    items[2].appid = 30;
    items[2].name = arena_strdup(&arena, "C");
    steam_search_results_t results = {items, 3};
    tui_apply_search_results(&state, results);
    TEST_ASSERT(state.screen == TUI_SCREEN_SELECT);
    TEST_ASSERT(state.select_cursor == 0);

    /* up at the top is a no-op */
    tui_handle_key(&state, 'k', 0);
    TEST_ASSERT(state.select_cursor == 0);

    tui_handle_key(&state, 'j', 0);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.select_cursor == 2);

    /* down at the bottom is a no-op */
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.select_cursor == 2);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_LOAD_GAME);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_GAME);
    TEST_ASSERT(state.appid == 30);

    arena_free(&arena);
    return 0;
}

static int test_select_esc_goes_back_to_search(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "x", 0);

    steam_search_result_t items[2];
    items[0].appid = 1;
    items[0].name = arena_strdup(&arena, "A");
    items[1].appid = 2;
    items[1].name = arena_strdup(&arena, "B");
    steam_search_results_t results = {items, 2};
    tui_apply_search_results(&state, results);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.screen == TUI_SCREEN_SEARCH);

    arena_free(&arena);
    return 0;
}

static theme_set_t make_game_themes(arena_t *arena)
{
    theme_t *liked = (theme_t *)arena_alloc(arena, 2 * sizeof(theme_t));
    liked[0] = make_theme(arena, "building", "Building system");
    liked[1] = make_theme(arena, "co_op", "Co-op with friends");

    theme_t *disliked = (theme_t *)arena_alloc(arena, 1 * sizeof(theme_t));
    disliked[0] = make_theme(arena, "raids", "Enemy raids");

    theme_set_t themes;
    themes.liked = liked;
    themes.n_liked = 2;
    themes.disliked = disliked;
    themes.n_disliked = 1;
    return themes;
}

static int test_game_loaded_lands_on_liked(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    game.appid = 892970;
    game.name = arena_strdup(&arena, "Valheim");

    theme_set_t themes = make_game_themes(&arena);

    tui_apply_game_loaded(&state, 892970, game, themes);
    TEST_ASSERT(state.screen == TUI_SCREEN_LIKED);
    TEST_ASSERT(state.liked_cursor == 0);

    arena_free(&arena);
    return 0;
}

static int test_checklist_toggle_and_navigate(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    /* toggle item 0, move down, toggle item 1 */
    tui_handle_key(&state, ' ', 0);
    TEST_ASSERT(state.themes.liked[0].checked);
    TEST_ASSERT(!state.themes.liked[1].checked);

    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.liked_cursor == 1);
    tui_handle_key(&state, ' ', 0);
    TEST_ASSERT(state.themes.liked[1].checked);

    /* un-toggle item 1 */
    tui_handle_key(&state, ' ', 0);
    TEST_ASSERT(!state.themes.liked[1].checked);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.screen == TUI_SCREEN_DISLIKED);
    TEST_ASSERT(state.disliked_cursor == 0);

    tui_handle_key(&state, ' ', 0);
    TEST_ASSERT(state.themes.disliked[0].checked);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.screen == TUI_SCREEN_LIKED);

    arena_free(&arena);
    return 0;
}

static int test_disliked_enter_triggers_followup_generation(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);
    state.screen = TUI_SCREEN_DISLIKED;

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_GENERATE_FOLLOWUP);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_FOLLOWUP);

    arena_free(&arena);
    return 0;
}

static int test_apply_followup_generated_lands_on_followup_step(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;

    char *questions[2];
    questions[0] = (char *)"What moment stuck with you most?";
    questions[1] = (char *)"Would you play the sequel?";

    tui_apply_followup_generated(&state, questions, 2);

    TEST_ASSERT(state.screen == TUI_SCREEN_NOTES);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_FOLLOWUP);
    TEST_ASSERT(state.n_followup_questions == 2);
    TEST_ASSERT(state.followup_index == 0);
    TEST_ASSERT(state.followup_answer[0][0] == '\0');

    arena_free(&arena);
    return 0;
}

static int test_apply_followup_generated_zero_questions_skips_to_text(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;

    tui_apply_followup_generated(&state, NULL, 0);

    TEST_ASSERT(state.screen == TUI_SCREEN_NOTES);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_TEXT);

    arena_free(&arena);
    return 0;
}

static int test_apply_followup_failed_skips_to_text(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;

    tui_apply_followup_failed(&state);

    TEST_ASSERT(state.screen == TUI_SCREEN_NOTES);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_TEXT);
    TEST_ASSERT(state.n_followup_questions == 0);

    arena_free(&arena);
    return 0;
}

static int test_followup_step_answer_and_advance_to_next_question(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;

    char *questions[2];
    questions[0] = (char *)"What moment stuck with you most?";
    questions[1] = (char *)"Would you play the sequel?";
    tui_apply_followup_generated(&state, questions, 2);

    tui_handle_key(&state, 'y', 0);
    tui_handle_key(&state, 'e', 0);
    tui_handle_key(&state, 's', 0);
    TEST_ASSERT(strcmp(state.followup_answer[0], "yes") == 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.followup_index == 1);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_FOLLOWUP); /* still a question left */

    /* leave the last question blank (skip) and move on */
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_TEXT);

    arena_free(&arena);
    return 0;
}

static int test_followup_step_esc_goes_back_through_questions_then_to_disliked(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;

    char *questions[2];
    questions[0] = (char *)"What moment stuck with you most?";
    questions[1] = (char *)"Would you play the sequel?";
    tui_apply_followup_generated(&state, questions, 2);

    tui_handle_key(&state, 0, TUI_KEY_ENTER); /* move to question 2 */
    TEST_ASSERT(state.followup_index == 1);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.followup_index == 0);
    TEST_ASSERT(state.screen == TUI_SCREEN_NOTES);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.screen == TUI_SCREEN_DISLIKED);

    arena_free(&arena);
    return 0;
}

static int test_notes_text_esc_returns_to_last_followup_question_when_any(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;

    char *questions[1];
    questions[0] = (char *)"What moment stuck with you most?";
    tui_apply_followup_generated(&state, questions, 1);
    tui_handle_key(&state, 0, TUI_KEY_ENTER); /* skip straight to TEXT */
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_TEXT);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_FOLLOWUP);
    TEST_ASSERT(state.followup_index == 0);

    arena_free(&arena);
    return 0;
}

static int test_notes_text_esc_goes_to_disliked_when_no_followup(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);
    state.screen = TUI_SCREEN_LOADING_FOLLOWUP;
    tui_apply_followup_failed(&state); /* zero questions */

    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_TEXT);
    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.screen == TUI_SCREEN_DISLIKED);

    arena_free(&arena);
    return 0;
}

static int test_notes_flow_to_generate_review(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_TEXT;

    tui_handle_key(&state, 'h', 0);
    tui_handle_key(&state, 'i', 0);
    TEST_ASSERT(strcmp(state.notes_input, "hi") == 0);

    /* once there's content, Enter starts a new line rather than advancing */
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(strcmp(state.notes_input, "hi\n") == 0);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_TEXT);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_S) == TUI_ACTION_NONE);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_RECOMMEND);

    tui_handle_key(&state, 'y', 0);
    TEST_ASSERT(state.recommend == true);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_RATING);

    tui_handle_key(&state, '8', 0);
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.rating == 8);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_LENGTH);

    TEST_ASSERT(tui_handle_key(&state, 'm', 0) == TUI_ACTION_NONE);
    TEST_ASSERT(state.length == REVIEW_LEN_MEDIUM);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_VOCAB);

    TEST_ASSERT(tui_handle_key(&state, 's', 0) == TUI_ACTION_NONE);
    TEST_ASSERT(state.vocab == REVIEW_VOCAB_SIMPLE);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_FORMAT);

    TEST_ASSERT(tui_handle_key(&state, 'b', 0) == TUI_ACTION_PREDICT_SPOILER);
    TEST_ASSERT(state.bbcode == true);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_SPOILER_PREDICT);

    /* best-effort: a failed/empty guess just leaves the step blank */
    tui_apply_spoiler_predict_failed(&state);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_SPOILER);

    /* a blank spoiler field just means "no spoiler" -- Enter still advances */
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_GENERATE_REVIEW);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_REVIEW);

    arena_free(&arena);
    return 0;
}

static int test_rating_step_esc_and_blank_skip(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_RATING;

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_RECOMMEND);

    /* a blank rating just means "no rating given" -- Enter still advances */
    state.notes_step = TUI_NOTES_STEP_RATING;
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.rating == 0);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_LENGTH);

    /* a rating above 10 is clamped rather than rejected outright */
    state.notes_step = TUI_NOTES_STEP_RATING;
    state.rating_input[0] = '\0';
    state.rating_input_len = 0;
    state.rating_input_cursor = 0;
    tui_handle_key(&state, '9', 0);
    tui_handle_key(&state, '9', 0);
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.rating == 10);

    arena_free(&arena);
    return 0;
}

static int test_vocab_step_esc_goes_back_to_length(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_VOCAB;

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_LENGTH);

    arena_free(&arena);
    return 0;
}

static int test_format_step_esc_and_choice(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_FORMAT;

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_VOCAB);

    state.notes_step = TUI_NOTES_STEP_FORMAT;
    TEST_ASSERT(tui_handle_key(&state, 'p', 0) == TUI_ACTION_PREDICT_SPOILER);
    TEST_ASSERT(state.bbcode == false);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_SPOILER_PREDICT);

    arena_free(&arena);
    return 0;
}

static int test_format_step_skips_prediction_when_spoiler_already_typed(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    strcpy(state.spoiler_input, "my own typed spoiler");
    state.spoiler_input_len = strlen(state.spoiler_input);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_FORMAT;
    TEST_ASSERT(tui_handle_key(&state, 'p', 0) == TUI_ACTION_NONE);

    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_SPOILER);
    TEST_ASSERT(strcmp(state.spoiler_input, "my own typed spoiler") == 0);

    arena_free(&arena);
    return 0;
}

static int test_apply_spoiler_predicted_prefills_the_field(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_spoiler_predicted(&state, "The ending made me cry though.");

    TEST_ASSERT(state.screen == TUI_SCREEN_NOTES);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_SPOILER);
    TEST_ASSERT(strcmp(state.spoiler_input, "The ending made me cry though.") == 0);

    arena_free(&arena);
    return 0;
}

static int test_apply_spoiler_predicted_empty_guess_leaves_field_blank(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_spoiler_predicted(&state, "");

    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_SPOILER);
    TEST_ASSERT(state.spoiler_input_len == 0);

    arena_free(&arena);
    return 0;
}

static int test_apply_spoiler_predict_failed_leaves_field_blank(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_spoiler_predict_failed(&state);

    TEST_ASSERT(state.screen == TUI_SCREEN_NOTES);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_SPOILER);
    TEST_ASSERT(state.spoiler_input_len == 0);

    arena_free(&arena);
    return 0;
}

static int test_spoiler_step_esc_and_ctrl_s(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_SPOILER;

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_FORMAT);

    state.notes_step = TUI_NOTES_STEP_SPOILER;
    tui_handle_key(&state, 't', 0);
    tui_handle_key(&state, 'w', 0);
    tui_handle_key(&state, 'i', 0);
    tui_handle_key(&state, 's', 0);
    tui_handle_key(&state, 't', 0);
    TEST_ASSERT(strcmp(state.spoiler_input, "twist") == 0);

    /* once there's content, Enter starts a new line rather than submitting */
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(strcmp(state.spoiler_input, "twist\n") == 0);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_SPOILER);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_S) == TUI_ACTION_GENERATE_REVIEW);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_REVIEW);

    arena_free(&arena);
    return 0;
}

static int test_checklist_add_custom_item(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    TEST_ASSERT(state.themes.n_liked == 2);

    tui_handle_key(&state, 'n', 0);
    TEST_ASSERT(state.adding_item);

    tui_handle_key(&state, 'B', 0);
    tui_handle_key(&state, 'o', 0);
    tui_handle_key(&state, 's', 0);
    tui_handle_key(&state, 's', 0);
    TEST_ASSERT(strcmp(state.custom_item_input, "Boss") == 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(!state.adding_item);
    TEST_ASSERT(state.custom_item_input_len == 0);
    TEST_ASSERT(state.themes.n_liked == 3);
    TEST_ASSERT(strcmp(state.themes.liked[2].label, "Boss") == 0);
    TEST_ASSERT(state.themes.liked[2].checked); /* added items start checked */
    TEST_ASSERT(state.liked_cursor == 2);        /* cursor jumps to the new item */

    /* original items are untouched */
    TEST_ASSERT(strcmp(state.themes.liked[0].label, "Building system") == 0);
    TEST_ASSERT(strcmp(state.themes.liked[1].label, "Co-op with friends") == 0);

    arena_free(&arena);
    return 0;
}

static int test_checklist_add_custom_item_esc_cancels(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    tui_handle_key(&state, 'n', 0);
    tui_handle_key(&state, 'x', 0);
    TEST_ASSERT(strcmp(state.custom_item_input, "x") == 0);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(!state.adding_item);
    TEST_ASSERT(state.custom_item_input_len == 0);
    TEST_ASSERT(state.themes.n_liked == 2); /* nothing added */

    /* Esc cancelled the add, not the whole screen */
    TEST_ASSERT(state.screen == TUI_SCREEN_LIKED);

    arena_free(&arena);
    return 0;
}

static int test_checklist_add_blank_item_is_noop(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    tui_handle_key(&state, 'n', 0);
    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(!state.adding_item);
    TEST_ASSERT(state.themes.n_liked == 2); /* blank label adds nothing */

    arena_free(&arena);
    return 0;
}

static int test_checklist_move_item_to_other_side(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    TEST_ASSERT(state.themes.n_liked == 2);
    TEST_ASSERT(state.themes.n_disliked == 1);

    /* Mark "Building system" as something YOU actually want included, then
     * realize it's filed on the wrong side and move it over. */
    tui_handle_key(&state, ' ', 0);
    TEST_ASSERT(tui_handle_key(&state, 'm', 0) == TUI_ACTION_NONE);

    TEST_ASSERT(state.themes.n_liked == 1);
    TEST_ASSERT(strcmp(state.themes.liked[0].label, "Co-op with friends") == 0);

    TEST_ASSERT(state.themes.n_disliked == 2);
    TEST_ASSERT(strcmp(state.themes.disliked[1].label, "Building system") == 0);
    TEST_ASSERT(state.themes.disliked[1].checked); /* the checked flag moved with it */

    /* cursor clamped back into range on the now-shorter liked list */
    TEST_ASSERT(state.liked_cursor == 0);

    arena_free(&arena);
    return 0;
}

static int test_checklist_move_last_item_clamps_cursor(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    tui_handle_key(&state, 'j', 0); /* cursor to index 1, the last liked item */
    TEST_ASSERT(state.liked_cursor == 1);

    tui_handle_key(&state, 'm', 0);
    TEST_ASSERT(state.themes.n_liked == 1);
    TEST_ASSERT(state.liked_cursor == 0); /* clamped down, not left dangling at 1 */

    arena_free(&arena);
    return 0;
}

static int test_result_screen_edit_action(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_review_generated(&state, (char *)"a review");
    TEST_ASSERT(tui_handle_key(&state, 'e', 0) == TUI_ACTION_EDIT_REVIEW);
    TEST_ASSERT(state.screen == TUI_SCREEN_RESULT); /* app.c handles the actual edit, not state.c */

    arena_free(&arena);
    return 0;
}

static int test_result_screen_copy_action(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_review_generated(&state, (char *)"a review");
    TEST_ASSERT(tui_handle_key(&state, 'c', 0) == TUI_ACTION_COPY_REVIEW);
    TEST_ASSERT(state.screen == TUI_SCREEN_RESULT); /* app.c handles the actual copy, not state.c */

    arena_free(&arena);
    return 0;
}

static int test_result_screen_save_action(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_review_generated(&state, (char *)"a review");
    TEST_ASSERT(tui_handle_key(&state, 's', 0) == TUI_ACTION_SAVE_REVIEW);
    TEST_ASSERT(state.screen == TUI_SCREEN_RESULT); /* app.c handles the actual file write, not state.c */

    arena_free(&arena);
    return 0;
}

static int test_result_screen_change_answers_goes_to_liked(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_review_generated(&state, (char *)"a review");
    TEST_ASSERT(tui_handle_key(&state, 'o', 0) == TUI_ACTION_NONE);
    TEST_ASSERT(state.screen == TUI_SCREEN_LIKED);

    arena_free(&arena);
    return 0;
}

static int test_result_screen_regenerate_and_quit(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_review_generated(&state, (char *)"a review");
    TEST_ASSERT(state.screen == TUI_SCREEN_RESULT);

    TEST_ASSERT(tui_handle_key(&state, 'r', 0) == TUI_ACTION_GENERATE_REVIEW);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_REVIEW);

    state.screen = TUI_SCREEN_RESULT;
    TEST_ASSERT(tui_handle_key(&state, 'q', 0) == TUI_ACTION_QUIT);
    TEST_ASSERT(state.screen == TUI_SCREEN_QUIT);

    arena_free(&arena);
    return 0;
}

static int test_ctrl_c_quits_from_any_screen(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_C) == TUI_ACTION_QUIT);
    TEST_ASSERT(state.screen == TUI_SCREEN_QUIT);

    arena_free(&arena);
    return 0;
}

static int test_review_failed_lands_on_result_with_status(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 892970);

    tui_apply_review_failed(&state, "boom");
    TEST_ASSERT(state.screen == TUI_SCREEN_RESULT);
    TEST_ASSERT(strstr(state.status, "boom") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_f2_opens_settings_prefilled_from_cfg(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "valheim", 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_F2) == TUI_ACTION_NONE);
    TEST_ASSERT(state.screen == TUI_SCREEN_SETTINGS);
    TEST_ASSERT(state.settings_return_screen == TUI_SCREEN_SEARCH);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_PROVIDER);
    TEST_ASSERT(state.settings_provider_index == 0); /* "anthropic" */
    TEST_ASSERT(strcmp(state.settings_model, "claude-haiku-5-5") == 0);
    TEST_ASSERT(strcmp(state.settings_base_url, "") == 0);
    TEST_ASSERT(strcmp(state.settings_num_ctx, "8192") == 0);
    TEST_ASSERT(strcmp(state.settings_review_count, "200") == 0);
    TEST_ASSERT(strcmp(state.settings_steam_language, "english") == 0);
    TEST_ASSERT(strcmp(state.settings_steam_cache_ttl_hours, "24") == 0);
    TEST_ASSERT(strcmp(state.settings_steam_id, "") == 0);
    TEST_ASSERT(strcmp(state.settings_steam_api_key, "") == 0);
    TEST_ASSERT(state.settings_review_length_index == 0); /* "short" */
    TEST_ASSERT(state.settings_review_tone_index == 0);   /* "casual" */
    TEST_ASSERT(state.settings_review_vocab_index == 0);  /* "normal" */
    /* cursor starts at the end of the pre-filled text, like any editor */
    TEST_ASSERT(state.settings_model_cursor == state.settings_model_len);

    arena_free(&arena);
    return 0;
}

static int test_settings_field_navigation_and_provider_cycling(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    tui_handle_key(&state, 0, TUI_KEY_ARROW_RIGHT); /* anthropic -> openai */
    TEST_ASSERT(state.settings_provider_index == 1);
    tui_handle_key(&state, 0, TUI_KEY_ARROW_RIGHT); /* openai -> ollama */
    TEST_ASSERT(state.settings_provider_index == 2);
    tui_handle_key(&state, 0, TUI_KEY_ARROW_RIGHT); /* wraps back to anthropic */
    TEST_ASSERT(state.settings_provider_index == 0);
    tui_handle_key(&state, 0, TUI_KEY_ARROW_LEFT); /* wraps backward to ollama */
    TEST_ASSERT(state.settings_provider_index == 2);

    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_MODEL);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_BASE_URL);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_NUM_CTX);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_COUNT);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_LANGUAGE);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_CACHE_TTL_HOURS);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_ID);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_API_KEY);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_LENGTH);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_TONE);
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_VOCAB);
    tui_handle_key(&state, 'j', 0); /* bottom: no-op */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_VOCAB);
    tui_handle_key(&state, 'k', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_TONE);

    arena_free(&arena);
    return 0;
}

static int test_settings_edit_model_field(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);
    tui_handle_key(&state, 'j', 0); /* -> Model field */

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.settings_editing);

    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(state.settings_model, "") == 0);

    tui_handle_key(&state, 'q', 0);
    tui_handle_key(&state, 'w', 0);
    tui_handle_key(&state, 'e', 0);
    tui_handle_key(&state, 'n', 0);
    TEST_ASSERT(strcmp(state.settings_model, "qwen") == 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(!state.settings_editing);
    TEST_ASSERT(state.screen == TUI_SCREEN_SETTINGS); /* still on Settings, just out of edit mode */

    /* cfg itself is untouched until Ctrl+S */
    TEST_ASSERT(strcmp(cfg.llm_model, "claude-haiku-5-5") == 0);

    arena_free(&arena);
    return 0;
}

static int test_settings_num_ctx_field_is_numeric_only(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);
    tui_handle_key(&state, 'j', 0);
    tui_handle_key(&state, 'j', 0);
    tui_handle_key(&state, 'j', 0); /* -> Num ctx field */
    tui_handle_key(&state, 0, TUI_KEY_ENTER); /* edit */

    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(state.settings_num_ctx, "") == 0);

    tui_handle_key(&state, 'x', 0); /* rejected: not a digit */
    TEST_ASSERT(strcmp(state.settings_num_ctx, "") == 0);

    tui_handle_key(&state, '4', 0);
    tui_handle_key(&state, '0', 0);
    tui_handle_key(&state, '9', 0);
    tui_handle_key(&state, '6', 0);
    TEST_ASSERT(strcmp(state.settings_num_ctx, "4096") == 0);

    arena_free(&arena);
    return 0;
}

static int test_settings_esc_while_editing_reverts_field(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);
    tui_handle_key(&state, 'j', 0); /* -> Model field */
    tui_handle_key(&state, 0, TUI_KEY_ENTER);

    tui_handle_key(&state, 'x', 0);
    tui_handle_key(&state, 'x', 0);
    TEST_ASSERT(strstr(state.settings_model, "xx") != NULL);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(!state.settings_editing);
    TEST_ASSERT(strcmp(state.settings_model, "claude-haiku-5-5") == 0);

    arena_free(&arena);
    return 0;
}

static int test_settings_esc_in_nav_mode_discards_and_returns(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "valheim", 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    tui_handle_key(&state, 0, TUI_KEY_ARROW_RIGHT); /* change provider in the staging buffer only */
    TEST_ASSERT(state.settings_provider_index == 1);

    tui_handle_key(&state, 0, TUI_KEY_ESC);
    TEST_ASSERT(state.screen == TUI_SCREEN_SEARCH);
    TEST_ASSERT(strcmp(cfg.llm_provider, "anthropic") == 0); /* untouched */

    arena_free(&arena);
    return 0;
}

static int test_settings_ctrl_s_commits_to_cfg_and_returns_action(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, "valheim", 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    tui_handle_key(&state, 0, TUI_KEY_ARROW_RIGHT); /* -> openai */
    tui_handle_key(&state, 'j', 0);                 /* -> Model */
    tui_handle_key(&state, 0, TUI_KEY_ENTER);        /* edit */
    tui_handle_key(&state, 'x', 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);        /* confirm edit */

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_S) == TUI_ACTION_SAVE_SETTINGS);
    /* cfg is updated synchronously by the key handler; app.c still has to persist it to disk */
    TEST_ASSERT(strcmp(cfg.llm_provider, "openai") == 0);
    TEST_ASSERT(strstr(cfg.llm_model, "x") != NULL);

    tui_apply_settings_saved(&state);
    TEST_ASSERT(state.screen == TUI_SCREEN_SEARCH);
    TEST_ASSERT(strcmp(state.status, "") == 0);

    arena_free(&arena);
    return 0;
}

static int test_settings_save_failed_stays_on_settings_with_status(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    tui_apply_settings_save_failed(&state, "disk full");
    TEST_ASSERT(state.screen == TUI_SCREEN_SETTINGS);
    TEST_ASSERT(strstr(state.status, "disk full") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_settings_q_quits_in_nav_mode(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    TEST_ASSERT(tui_handle_key(&state, 'q', 0) == TUI_ACTION_QUIT);
    TEST_ASSERT(state.screen == TUI_SCREEN_QUIT);

    arena_free(&arena);
    return 0;
}

static int test_settings_review_count_field_edits_and_commits(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);
    tui_handle_key(&state, 'j', 0);
    tui_handle_key(&state, 'j', 0);
    tui_handle_key(&state, 'j', 0);
    tui_handle_key(&state, 'j', 0); /* -> Review count field */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_COUNT);
    TEST_ASSERT(strcmp(state.settings_review_count, "200") == 0);

    tui_handle_key(&state, 0, TUI_KEY_ENTER); /* edit */
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 'x', 0); /* rejected: not a digit */
    TEST_ASSERT(strcmp(state.settings_review_count, "") == 0);
    tui_handle_key(&state, '5', 0);
    tui_handle_key(&state, '0', 0);
    TEST_ASSERT(strcmp(state.settings_review_count, "50") == 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER); /* confirm edit */

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_S) == TUI_ACTION_SAVE_SETTINGS);
    TEST_ASSERT(cfg.steam_review_count == 50);

    arena_free(&arena);
    return 0;
}

static int test_settings_new_steam_and_review_fields_edit_and_commit(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    for (int i = 0; i < 5; i++) tui_handle_key(&state, 'j', 0); /* -> Steam language */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_LANGUAGE);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 'p', 0);
    tui_handle_key(&state, 'o', 0);
    tui_handle_key(&state, 'l', 0);
    tui_handle_key(&state, 'i', 0);
    tui_handle_key(&state, 's', 0);
    tui_handle_key(&state, 'h', 0);
    TEST_ASSERT(strcmp(state.settings_steam_language, "polish") == 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);

    tui_handle_key(&state, 'j', 0); /* -> Cache TTL hours */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_CACHE_TTL_HOURS);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    tui_handle_key(&state, '4', 0);
    tui_handle_key(&state, '8', 0);
    TEST_ASSERT(strcmp(state.settings_steam_cache_ttl_hours, "48") == 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);

    tui_handle_key(&state, 'j', 0); /* -> Steam ID */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_ID);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);
    tui_handle_key(&state, 'a', 0); /* rejected: not a digit */
    tui_handle_key(&state, '7', 0);
    tui_handle_key(&state, '6', 0);
    TEST_ASSERT(strcmp(state.settings_steam_id, "76") == 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);

    tui_handle_key(&state, 'j', 0); /* -> Steam API key */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_STEAM_API_KEY);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);
    tui_handle_key(&state, 'A', 0);
    tui_handle_key(&state, 'C', 0);
    tui_handle_key(&state, '1', 0);
    TEST_ASSERT(strcmp(state.settings_steam_api_key, "AC1") == 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);

    tui_handle_key(&state, 'j', 0); /* -> Length (short -> medium) */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_LENGTH);
    tui_handle_key(&state, 'l', 0);
    TEST_ASSERT(state.settings_review_length_index == 1);

    tui_handle_key(&state, 'j', 0); /* -> Tone (casual -> neutral) */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_TONE);
    tui_handle_key(&state, 'l', 0);
    TEST_ASSERT(state.settings_review_tone_index == 1);

    tui_handle_key(&state, 'j', 0); /* -> Vocab (normal -> simple) */
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_VOCAB);
    tui_handle_key(&state, 'l', 0);
    TEST_ASSERT(state.settings_review_vocab_index == 1);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_S) == TUI_ACTION_SAVE_SETTINGS);

    TEST_ASSERT(strcmp(cfg.steam_language, "polish") == 0);
    TEST_ASSERT(cfg.steam_cache_ttl_hours == 48);
    TEST_ASSERT(strcmp(cfg.steam_id, "76") == 0);
    TEST_ASSERT(strcmp(cfg.steam_api_key, "AC1") == 0);
    TEST_ASSERT(cfg.steam_api_key_from_env == false);
    TEST_ASSERT(strcmp(cfg.review_default_length, "medium") == 0);
    TEST_ASSERT(strcmp(cfg.review_default_tone, "neutral") == 0);
    TEST_ASSERT(strcmp(cfg.review_default_vocab, "simple") == 0);

    arena_free(&arena);
    return 0;
}

static int test_settings_unedited_steam_api_key_keeps_its_env_source(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    cfg.steam_api_key = arena_strdup(&arena, "env-sourced-key");
    cfg.steam_api_key_from_env = true;

    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);
    tui_handle_key(&state, 0, TUI_KEY_F2);

    /* pre-filled from cfg, same as it would be either way */
    TEST_ASSERT(strcmp(state.settings_steam_api_key, "env-sourced-key") == 0);

    /* edit something unrelated (Model), then save, without ever touching
     * the key field */
    tui_handle_key(&state, 'j', 0);
    TEST_ASSERT(state.settings_field == TUI_SETTINGS_FIELD_MODEL);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);
    tui_handle_key(&state, 'x', 0);
    tui_handle_key(&state, 0, TUI_KEY_ENTER);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_S) == TUI_ACTION_SAVE_SETTINGS);

    /* the key and its env provenance are untouched -- never silently
     * copied into config.ini just because some other field was saved */
    TEST_ASSERT(strcmp(cfg.steam_api_key, "env-sourced-key") == 0);
    TEST_ASSERT(cfg.steam_api_key_from_env == true);

    arena_free(&arena);
    return 0;
}

static int test_text_field_arrow_keys_edit_in_the_middle(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg = make_fake_config(&arena);
    app_state_t state;
    tui_state_init(&state, &arena, &cfg, NULL, 0);

    tui_handle_key(&state, 'h', 0);
    tui_handle_key(&state, 'e', 0);
    tui_handle_key(&state, 'l', 0);
    tui_handle_key(&state, 'l', 0);
    tui_handle_key(&state, 'o', 0);
    TEST_ASSERT(strcmp(state.search_input, "hello") == 0);
    TEST_ASSERT(state.search_input_cursor == 5);

    /* a space, pressed at the end, must move the cursor just like any
     * other character -- this is the regression that prompted this test:
     * the cursor used to visibly lag behind by one character after a
     * space, catching up only once another character was typed. */
    tui_handle_key(&state, ' ', 0);
    TEST_ASSERT(strcmp(state.search_input, "hello ") == 0);
    TEST_ASSERT(state.search_input_cursor == 6);

    tui_handle_key(&state, 0, TUI_KEY_ARROW_LEFT);
    tui_handle_key(&state, 0, TUI_KEY_ARROW_LEFT);
    tui_handle_key(&state, 0, TUI_KEY_ARROW_LEFT);
    TEST_ASSERT(state.search_input_cursor == 3); /* "hel|lo " */

    /* insert at the cursor, not at the end */
    tui_handle_key(&state, 'p', 0);
    TEST_ASSERT(strcmp(state.search_input, "helplo ") == 0);
    TEST_ASSERT(state.search_input_cursor == 4);

    /* forward delete removes the character at the cursor */
    tui_handle_key(&state, 0, TUI_KEY_DELETE);
    TEST_ASSERT(strcmp(state.search_input, "helpo ") == 0);

    /* Home/End jump to the start/end */
    tui_handle_key(&state, 0, TUI_KEY_HOME);
    TEST_ASSERT(state.search_input_cursor == 0);
    tui_handle_key(&state, 0, TUI_KEY_END);
    TEST_ASSERT(state.search_input_cursor == state.search_input_len);

    /* arrows can't run off either end */
    tui_handle_key(&state, 0, TUI_KEY_ARROW_RIGHT);
    TEST_ASSERT(state.search_input_cursor == state.search_input_len);
    for (size_t i = 0; i < 20; i++) tui_handle_key(&state, 0, TUI_KEY_ARROW_LEFT);
    TEST_ASSERT(state.search_input_cursor == 0);

    /* backspace at the very start is a no-op */
    tui_handle_key(&state, 0, TUI_KEY_BACKSPACE);
    TEST_ASSERT(strcmp(state.search_input, "helpo ") == 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_init_prefills_search()) return 1;
    if (test_init_with_appid_skips_search()) return 1;
    if (test_search_typing_and_enter()) return 1;
    if (test_apply_search_results_zero_one_many()) return 1;
    if (test_select_screen_navigation()) return 1;
    if (test_select_esc_goes_back_to_search()) return 1;
    if (test_game_loaded_lands_on_liked()) return 1;
    if (test_checklist_toggle_and_navigate()) return 1;
    if (test_format_step_esc_and_choice()) return 1;
    if (test_format_step_skips_prediction_when_spoiler_already_typed()) return 1;
    if (test_apply_spoiler_predicted_prefills_the_field()) return 1;
    if (test_apply_spoiler_predicted_empty_guess_leaves_field_blank()) return 1;
    if (test_apply_spoiler_predict_failed_leaves_field_blank()) return 1;
    if (test_spoiler_step_esc_and_ctrl_s()) return 1;
    if (test_checklist_add_custom_item()) return 1;
    if (test_checklist_add_custom_item_esc_cancels()) return 1;
    if (test_checklist_add_blank_item_is_noop()) return 1;
    if (test_checklist_move_item_to_other_side()) return 1;
    if (test_checklist_move_last_item_clamps_cursor()) return 1;
    if (test_disliked_enter_triggers_followup_generation()) return 1;
    if (test_apply_followup_generated_lands_on_followup_step()) return 1;
    if (test_apply_followup_generated_zero_questions_skips_to_text()) return 1;
    if (test_apply_followup_failed_skips_to_text()) return 1;
    if (test_followup_step_answer_and_advance_to_next_question()) return 1;
    if (test_followup_step_esc_goes_back_through_questions_then_to_disliked()) return 1;
    if (test_notes_text_esc_returns_to_last_followup_question_when_any()) return 1;
    if (test_notes_text_esc_goes_to_disliked_when_no_followup()) return 1;
    if (test_notes_flow_to_generate_review()) return 1;
    if (test_rating_step_esc_and_blank_skip()) return 1;
    if (test_vocab_step_esc_goes_back_to_length()) return 1;
    if (test_result_screen_edit_action()) return 1;
    if (test_result_screen_copy_action()) return 1;
    if (test_result_screen_save_action()) return 1;
    if (test_result_screen_change_answers_goes_to_liked()) return 1;
    if (test_result_screen_regenerate_and_quit()) return 1;
    if (test_ctrl_c_quits_from_any_screen()) return 1;
    if (test_review_failed_lands_on_result_with_status()) return 1;
    if (test_f2_opens_settings_prefilled_from_cfg()) return 1;
    if (test_settings_field_navigation_and_provider_cycling()) return 1;
    if (test_settings_edit_model_field()) return 1;
    if (test_settings_num_ctx_field_is_numeric_only()) return 1;
    if (test_settings_esc_while_editing_reverts_field()) return 1;
    if (test_settings_esc_in_nav_mode_discards_and_returns()) return 1;
    if (test_settings_ctrl_s_commits_to_cfg_and_returns_action()) return 1;
    if (test_settings_save_failed_stays_on_settings_with_status()) return 1;
    if (test_settings_q_quits_in_nav_mode()) return 1;
    if (test_settings_review_count_field_edits_and_commits()) return 1;
    if (test_settings_new_steam_and_review_fields_edit_and_commit()) return 1;
    if (test_settings_unedited_steam_api_key_keeps_its_env_source()) return 1;
    if (test_text_field_arrow_keys_edit_in_the_middle()) return 1;

    printf("all tests passed\n");
    return 0;
}

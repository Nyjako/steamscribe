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

static int test_init_prefills_search(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, "valheim", 0);

    TEST_ASSERT(state.screen == TUI_SCREEN_SEARCH);
    TEST_ASSERT(strcmp(state.search_input, "valheim") == 0);

    arena_free(&arena);
    return 0;
}

static int test_init_with_appid_skips_search(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 892970);

    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_GAME);
    TEST_ASSERT(state.appid == 892970);

    arena_free(&arena);
    return 0;
}

static int test_search_typing_and_enter(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 0);

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

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, "x", 0);

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

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, "x", 0);

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

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, "x", 0);

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

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 892970);

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

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 892970);

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

static int test_notes_flow_to_generate_review(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 892970);

    game_info_t game;
    memset(&game, 0, sizeof(game));
    theme_set_t themes = make_game_themes(&arena);
    tui_apply_game_loaded(&state, 892970, game, themes);

    state.screen = TUI_SCREEN_NOTES;
    state.notes_step = TUI_NOTES_STEP_TEXT;

    tui_handle_key(&state, 'h', 0);
    tui_handle_key(&state, 'i', 0);
    TEST_ASSERT(strcmp(state.notes_input, "hi") == 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_ENTER) == TUI_ACTION_NONE);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_RECOMMEND);

    tui_handle_key(&state, 'y', 0);
    TEST_ASSERT(state.recommend == true);
    TEST_ASSERT(state.notes_step == TUI_NOTES_STEP_LENGTH);

    TEST_ASSERT(tui_handle_key(&state, 'm', 0) == TUI_ACTION_GENERATE_REVIEW);
    TEST_ASSERT(state.length == REVIEW_LEN_MEDIUM);
    TEST_ASSERT(state.screen == TUI_SCREEN_LOADING_REVIEW);

    arena_free(&arena);
    return 0;
}

static int test_result_screen_regenerate_and_quit(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 892970);

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

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 0);

    TEST_ASSERT(tui_handle_key(&state, 0, TUI_KEY_CTRL_C) == TUI_ACTION_QUIT);
    TEST_ASSERT(state.screen == TUI_SCREEN_QUIT);

    arena_free(&arena);
    return 0;
}

static int test_review_failed_lands_on_result_with_status(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    app_state_t state;
    tui_state_init(&state, &arena, REVIEW_TONE_CASUAL, NULL, 892970);

    tui_apply_review_failed(&state, "boom");
    TEST_ASSERT(state.screen == TUI_SCREEN_RESULT);
    TEST_ASSERT(strstr(state.status, "boom") != NULL);

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
    if (test_notes_flow_to_generate_review()) return 1;
    if (test_result_screen_regenerate_and_quit()) return 1;
    if (test_ctrl_c_quits_from_any_screen()) return 1;
    if (test_review_failed_lands_on_result_with_status()) return 1;

    printf("all tests passed\n");
    return 0;
}

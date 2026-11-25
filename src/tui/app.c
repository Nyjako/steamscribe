#include "tui/app.h"

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <termbox2.h>

#include "core/pipeline.h"
#include "core/review.h"
#include "tui/state.h"
#include "tui/widgets.h"

static review_tone_t tone_from_config(const config_t *cfg)
{
    if (strcmp(cfg->review_default_tone, "neutral") == 0) return REVIEW_TONE_NEUTRAL;
    if (strcmp(cfg->review_default_tone, "enthusiastic") == 0) return REVIEW_TONE_ENTHUSIASTIC;
    return REVIEW_TONE_CASUAL;
}

static void handle_fatal_signal(int sig)
{
    tb_shutdown();
    _exit(128 + sig);
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_fatal_signal;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

static bool is_loading_screen(tui_screen_t screen)
{
    return screen == TUI_SCREEN_LOADING_GAME || screen == TUI_SCREEN_LOADING_REVIEW;
}

static void draw(app_state_t *state)
{
    tb_clear();

    switch (state->screen)
    {
        case TUI_SCREEN_SEARCH:
            widgets_draw_header(1, "steamscribe -- search for a game");
            widgets_draw_text_input(3, "Game name >", state->search_input);
            widgets_draw_status_bar(state->status);
            break;

        case TUI_SCREEN_SELECT:
            widgets_draw_header(1, "Pick a game (Enter), Esc to search again, q to quit");
            widgets_draw_search_results(3, &state->search_results, state->select_cursor);
            widgets_draw_status_bar(state->status);
            break;

        case TUI_SCREEN_LOADING_GAME:
            widgets_draw_header(1, "Loading...");
            tb_print(2, 3, TB_DEFAULT, TB_DEFAULT, "Fetching game info and reading player reviews...");
            break;

        case TUI_SCREEN_LIKED:
            widgets_draw_header(1, "What did YOU like? Space toggles, Enter continues, Esc back, q quits");
            widgets_draw_checklist(3, state->themes.liked, state->themes.n_liked, state->liked_cursor);
            break;

        case TUI_SCREEN_DISLIKED:
            widgets_draw_header(1, "What bothered you? Space toggles, Enter continues, Esc back, q quits");
            widgets_draw_checklist(3, state->themes.disliked, state->themes.n_disliked, state->disliked_cursor);
            break;

        case TUI_SCREEN_NOTES:
            widgets_draw_header(1, "A few more questions");

            if (state->notes_step == TUI_NOTES_STEP_TEXT)
            {
                widgets_draw_text_input(3, "Anything else? >", state->notes_input);
            }
            else
            {
                tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "Anything else? > %s", state->notes_input);
            }

            if (state->notes_step >= TUI_NOTES_STEP_RECOMMEND)
            {
                const char *answer = (state->notes_step == TUI_NOTES_STEP_RECOMMEND) ? "" : (state->recommend ? "y" : "n");
                tb_printf(2, 4, TB_DEFAULT, TB_DEFAULT, "Recommend it? (y/n) > %s", answer);
            }

            if (state->notes_step == TUI_NOTES_STEP_LENGTH)
            {
                tb_print(2, 5, TB_DEFAULT, TB_DEFAULT, "Length: (s)hort / (m)edium / (l)ong >");
            }
            break;

        case TUI_SCREEN_LOADING_REVIEW:
            widgets_draw_header(1, "Writing your review...");
            break;

        case TUI_SCREEN_RESULT:
            widgets_draw_header(1, "Your review -- (r) regenerate, (q) quit");
            if (state->review_text) widgets_draw_wrapped_text(state->arena, 2, 3, tb_width() - 4, state->review_text);
            widgets_draw_status_bar(state->status);
            break;

        case TUI_SCREEN_QUIT:
        default:
            break;
    }

    tb_present();
}

static void do_search(app_state_t *state, const config_t *cfg)
{
    steam_search_results_t results;
    err_t err;
    err_clear(&err);

    if (steam_search(state->arena, state->search_input, cfg->steam_language, &results, &err) != 0)
    {
        tui_apply_search_failed(state, err.msg);
        return;
    }

    tui_apply_search_results(state, results);
}

static void do_load_game(app_state_t *state, const config_t *cfg)
{
    err_t err;
    err_clear(&err);

    game_info_t game;
    if (pipeline_fetch_game(state->arena, cfg, state->appid, &game, &err) != 0)
    {
        tui_apply_game_load_failed(state, err.msg);
        return;
    }

    theme_set_t themes;
    if (pipeline_fetch_themes(state->arena, cfg, state->appid, &game, &themes, &err) != 0)
    {
        tui_apply_game_load_failed(state, err.msg);
        return;
    }

    tui_apply_game_loaded(state, state->appid, game, themes);
}

static void do_generate_review(app_state_t *state, const config_t *cfg)
{
    err_t err;
    err_clear(&err);

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    if (pipeline_resolve_llm(cfg, &provider, &llm_cfg, &err) != 0)
    {
        tui_apply_review_failed(state, err.msg);
        return;
    }

    review_request_t req;
    req.game = &state->game;
    req.themes = &state->themes;
    req.notes = state->notes_input;
    req.recommend = state->recommend;
    req.length = state->length;
    req.tone = state->tone;

    char *review_text = NULL;
    if (review_generate(state->arena, provider, &llm_cfg, &req, &review_text, &err) != 0)
    {
        tui_apply_review_failed(state, err.msg);
        return;
    }

    tui_apply_review_generated(state, review_text);
}

int tui_run(arena_t *arena, const config_t *cfg, const tui_options_t *options, err_t *err)
{
    if (tb_init() != 0)
    {
        err_set(err, -1, "could not initialize the terminal (termbox2)");
        return -1;
    }

    install_signal_handlers();

    app_state_t state;
    tui_state_init(&state, arena, tone_from_config(cfg), options->initial_query, options->initial_appid);

    while (state.screen != TUI_SCREEN_QUIT)
    {
        if (is_loading_screen(state.screen))
        {
            draw(&state);

            if (state.screen == TUI_SCREEN_LOADING_GAME) do_load_game(&state, cfg);
            else do_generate_review(&state, cfg);

            continue;
        }

        draw(&state);

        struct tb_event ev;
        if (tb_poll_event(&ev) != TB_OK) continue;
        if (ev.type != TB_EVENT_KEY) continue; /* TB_EVENT_RESIZE: next loop redraws at the new size */

        tui_action_t action = tui_handle_key(&state, ev.ch, ev.key);
        if (action == TUI_ACTION_SEARCH) do_search(&state, cfg);
    }

    tb_shutdown();
    return 0;
}

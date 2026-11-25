#include "tui/app.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <termbox2.h>

#include "core/pipeline.h"
#include "core/review.h"
#include "tui/keys.h"
#include "tui/state.h"
#include "tui/widgets.h"

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

static void draw_settings_field(int y, const char *label, const char *value, bool focused, bool editing)
{
    uintattr_t fg = focused ? TB_BLACK : TB_DEFAULT;
    uintattr_t bg = focused ? TB_CYAN : TB_DEFAULT;
    tb_printf(2, y, fg, bg, "%-10s %s%s", label, value, (focused && editing) ? "_" : "");
}

static void draw(app_state_t *state, char spinner_ch)
{
    tb_clear();

    switch (state->screen)
    {
        case TUI_SCREEN_SEARCH:
            widgets_draw_header(1, "steamscribe -- search for a game (F2 for settings)");
            widgets_draw_text_input(3, "Game name >", state->search_input);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_SELECT:
            widgets_draw_header(1, "Pick a game (Enter), Esc to search again, q to quit");
            widgets_draw_search_results(3, &state->search_results, state->select_cursor);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_LOADING_GAME:
            widgets_draw_header(1, "Loading...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c Fetching game info and reading player reviews...", spinner_ch);
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
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c This can take a while on a local model -- Ctrl+C to give up", spinner_ch);
            break;

        case TUI_SCREEN_RESULT:
            widgets_draw_header(1, "Your review -- (r) regenerate, (q) quit");
            if (state->review_text) widgets_draw_wrapped_text(state->arena, 2, 3, tb_width() - 4, state->review_text);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_SETTINGS:
        {
            widgets_draw_header(1, "Settings -- j/k fields, h/l or Enter cycles provider, Enter edits, Ctrl+S saves, Esc cancels");

            static const char *provider_names[] = {"anthropic", "openai", "ollama"};
            draw_settings_field(3, "Provider:", provider_names[state->settings_provider_index], state->settings_field == TUI_SETTINGS_FIELD_PROVIDER, false);
            draw_settings_field(4, "Model:", state->settings_model, state->settings_field == TUI_SETTINGS_FIELD_MODEL, state->settings_editing);
            draw_settings_field(5, "Base URL:", state->settings_base_url, state->settings_field == TUI_SETTINGS_FIELD_BASE_URL, state->settings_editing);
            draw_settings_field(6, "Num ctx:", state->settings_num_ctx, state->settings_field == TUI_SETTINGS_FIELD_NUM_CTX, state->settings_editing);

            tb_print(2, 8, TB_DEFAULT, TB_DEFAULT, "Base URL: empty uses the provider's default. Num ctx: ollama only.");
            tb_print(2, 9, TB_DEFAULT, TB_DEFAULT, "API keys come only from ANTHROPIC_API_KEY / OPENAI_API_KEY -- not editable here.");
            widgets_draw_status_bar(state->arena, state->status);
            break;
        }

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

/* Loading-game and loading-review both run on a background thread so the
 * main thread can keep the terminal responsive (animate a spinner, notice
 * Ctrl+C) during a slow network fetch or a slow local LLM call -- local
 * CPU inference especially can take well past what a frozen "Loading..."
 * screen should ever make someone sit through. Only the background thread
 * allocates from `state->arena` while it's running; the main thread only
 * reads `state` fields that are already set (never the ones the job is
 * about to fill in), so this works without the arena itself needing to be
 * thread-safe. */
typedef struct bg_result_t
{
    pthread_mutex_t mutex;
    bool done;
    int rc;
    err_t err;
    game_info_t game;
    theme_set_t themes;
    char *review_text;
} bg_result_t;

typedef struct bg_ctx_t
{
    app_state_t *state;
    const config_t *cfg;
    bg_result_t *result;
} bg_ctx_t;

static void *bg_load_game_thread(void *argp)
{
    bg_ctx_t *ctx = (bg_ctx_t *)argp;
    bg_result_t *result = ctx->result;

    err_t err;
    err_clear(&err);

    game_info_t game;
    int rc = pipeline_fetch_game(ctx->state->arena, ctx->cfg, ctx->state->appid, &game, &err);

    theme_set_t themes;
    memset(&themes, 0, sizeof(themes));
    if (rc == 0) rc = pipeline_fetch_themes(ctx->state->arena, ctx->cfg, ctx->state->appid, &game, &themes, &err);

    pthread_mutex_lock(&result->mutex);
    result->rc = rc;
    result->err = err;
    if (rc == 0)
    {
        result->game = game;
        result->themes = themes;
    }
    result->done = true;
    pthread_mutex_unlock(&result->mutex);

    free(ctx);
    return NULL;
}

static void *bg_generate_review_thread(void *argp)
{
    bg_ctx_t *ctx = (bg_ctx_t *)argp;
    bg_result_t *result = ctx->result;

    err_t err;
    err_clear(&err);

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    int rc = pipeline_resolve_llm(ctx->cfg, &provider, &llm_cfg, &err);

    char *review_text = NULL;
    if (rc == 0)
    {
        review_request_t req;
        req.game = &ctx->state->game;
        req.themes = &ctx->state->themes;
        req.notes = ctx->state->notes_input;
        req.recommend = ctx->state->recommend;
        req.length = ctx->state->length;
        req.tone = ctx->state->tone;

        rc = review_generate(ctx->state->arena, provider, &llm_cfg, &req, &review_text, &err);
    }

    pthread_mutex_lock(&result->mutex);
    result->rc = rc;
    result->err = err;
    result->review_text = review_text;
    result->done = true;
    pthread_mutex_unlock(&result->mutex);

    free(ctx);
    return NULL;
}

static void do_save_settings(app_state_t *state)
{
    err_t err;
    err_clear(&err);

    if (config_save(state->cfg, &err) != 0)
    {
        tui_apply_settings_save_failed(state, err.msg);
        return;
    }

    tui_apply_settings_saved(state);
}

static bool bg_result_is_done(bg_result_t *result)
{
    pthread_mutex_lock(&result->mutex);
    bool done = result->done;
    pthread_mutex_unlock(&result->mutex);
    return done;
}

/* Runs `thread_fn` in the background while animating a spinner over the
 * already-drawn Loading screen, then folds the result into `state` once
 * it's done. A Ctrl+C while waiting exits immediately (the OS cleans up
 * the still-running network call; there's no cancellation plumbing for it). */
static void run_loading_screen(app_state_t *state, const config_t *cfg, void *(*thread_fn)(void *))
{
    static const char SPINNER_FRAMES[] = "|/-\\";

    bg_result_t result;
    memset(&result, 0, sizeof(result));
    pthread_mutex_init(&result.mutex, NULL);

    bg_ctx_t *ctx = (bg_ctx_t *)malloc(sizeof(bg_ctx_t));
    ctx->state = state;
    ctx->cfg = cfg;
    ctx->result = &result;

    pthread_t thread;
    pthread_create(&thread, NULL, thread_fn, ctx);

    size_t spinner_i = 0;
    while (!bg_result_is_done(&result))
    {
        draw(state, SPINNER_FRAMES[spinner_i % (sizeof(SPINNER_FRAMES) - 1)]);
        spinner_i++;

        struct tb_event ev;
        if (tb_peek_event(&ev, 120) == TB_OK && ev.type == TB_EVENT_KEY && ev.key == TUI_KEY_CTRL_C)
        {
            tb_shutdown();
            _exit(130);
        }
    }

    pthread_join(thread, NULL);
    pthread_mutex_destroy(&result.mutex);

    if (state->screen == TUI_SCREEN_LOADING_GAME)
    {
        if (result.rc == 0) tui_apply_game_loaded(state, state->appid, result.game, result.themes);
        else tui_apply_game_load_failed(state, result.err.msg);
    }
    else
    {
        if (result.rc == 0) tui_apply_review_generated(state, result.review_text);
        else tui_apply_review_failed(state, result.err.msg);
    }
}

int tui_run(arena_t *arena, config_t *cfg, const tui_options_t *options, err_t *err)
{
    if (tb_init() != 0)
    {
        err_set(err, -1, "could not initialize the terminal (termbox2)");
        return -1;
    }

    install_signal_handlers();

    app_state_t state;
    tui_state_init(&state, arena, cfg, options->initial_query, options->initial_appid);

    while (state.screen != TUI_SCREEN_QUIT)
    {
        if (is_loading_screen(state.screen))
        {
            tui_screen_t loading_screen = state.screen;
            run_loading_screen(&state, cfg, loading_screen == TUI_SCREEN_LOADING_GAME ? bg_load_game_thread : bg_generate_review_thread);
            continue;
        }

        draw(&state, ' ');

        struct tb_event ev;
        if (tb_poll_event(&ev) != TB_OK) continue;
        if (ev.type != TB_EVENT_KEY) continue; /* TB_EVENT_RESIZE: next loop redraws at the new size */

        tui_action_t action = tui_handle_key(&state, ev.ch, ev.key);
        if (action == TUI_ACTION_SEARCH) do_search(&state, cfg);
        else if (action == TUI_ACTION_SAVE_SETTINGS) do_save_settings(&state);
    }

    tb_shutdown();
    return 0;
}

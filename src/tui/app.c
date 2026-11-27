#include "tui/app.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <termbox2.h>

#include <goblin/appdirs.h>

#include "core/followup.h"
#include "core/pipeline.h"
#include "core/review.h"
#include "core/spoiler_predict.h"
#include "tui/keys.h"
#include "tui/state.h"
#include "tui/widgets.h"

/* Mirrors the fixed provider list tui/state.c cycles the Settings "Provider"
 * field through (that list is private to state.c -- this is app.c's own
 * copy, used both to draw the field's current choice and to resolve it to
 * an actual provider name for the "test connection" action). */
static const char *SETTINGS_PROVIDER_NAMES[] = {"anthropic", "openai", "ollama"};

/* Set once tui_run() has a live app_state_t, so a fatal signal's handler
 * (which gets no arguments of its own) can still recover whatever status
 * or error text was last shown, and print it after the terminal is
 * restored instead of just losing it along with the alternate screen. */
static app_state_t *g_signal_state = NULL;

static void handle_fatal_signal(int sig)
{
    tb_shutdown();
    if (g_signal_state && g_signal_state->status[0] != '\0')
    {
        size_t len = strlen(g_signal_state->status);
        (void)write(STDERR_FILENO, g_signal_state->status, len);
        (void)write(STDERR_FILENO, "\n", 1);
    }
    _exit(128 + sig);
}

/* Prints whatever's currently in the status bar to stderr, so an error
 * shown there (e.g. "Could not load game: ...") survives past the TUI
 * tearing down the alternate screen -- whether the app exits normally or
 * the user Ctrl+C's out of it. A no-op when the status is just empty. */
static void flush_pending_status(const app_state_t *state)
{
    if (state->status[0] != '\0') fprintf(stderr, "%s\n", state->status);
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
    return screen == TUI_SCREEN_LOADING_LIBRARY || screen == TUI_SCREEN_LOADING_GAME || screen == TUI_SCREEN_LOADING_FOLLOWUP || screen == TUI_SCREEN_LOADING_SPOILER_PREDICT ||
           screen == TUI_SCREEN_LOADING_REVIEW || screen == TUI_SCREEN_LOADING_TEST_CONNECTION;
}

/* `mask`, when true and the field isn't being actively edited, displays
 * one asterisk per character instead of `value` itself -- so a secret
 * typed in here (the Steam API key) isn't left sitting on screen once you
 * tab away from it, while still showing it plainly while you're typing or
 * pasting it in, to actually see what you entered. The substitution is
 * 1:1 so the cursor math below doesn't need to change for it. */
static void draw_settings_field(int y, const char *label, const char *value, size_t cursor, bool focused, bool editing, bool mask)
{
    uintattr_t fg = focused ? TB_BLACK : TB_DEFAULT;
    uintattr_t bg = focused ? TB_CYAN : TB_DEFAULT;

    const char *shown = value;
    char masked[128];
    if (mask && !editing && value[0])
    {
        size_t n = strlen(value);
        if (n >= sizeof(masked)) n = sizeof(masked) - 1;
        memset(masked, '*', n);
        masked[n] = '\0';
        shown = masked;
    }

    tb_printf(2, y, fg, bg, "%-10s %s", label, shown);
    if (focused && editing) tb_set_cursor(2 + 11 + (int)cursor, y);
}

static void draw(app_state_t *state, char spinner_ch)
{
    tb_clear();

    switch (state->screen)
    {
        case TUI_SCREEN_SEARCH:
        {
            int width = tb_width() - 4;
            if (width < 1) width = 1;

            widgets_draw_header(1, "steamscribe -- search for a game (F2 settings, F3 browse your library by playtime)");
            tb_print(2, 3, TB_DEFAULT, TB_DEFAULT, "Game name >");
            widgets_draw_text_area(4, width, state->search_input, state->search_input_cursor);
            widgets_draw_status_bar(state->arena, state->status);
            break;
        }

        case TUI_SCREEN_SELECT:
            widgets_draw_header(1, "Pick a game (Enter), Esc to search again, q to quit");
            widgets_draw_search_results(3, &state->search_results, state->select_cursor);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_LOADING_LIBRARY:
            widgets_draw_header(1, "Loading your library...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c Fetching your owned games and playtime...", spinner_ch);
            break;

        case TUI_SCREEN_LIBRARY:
            widgets_draw_header(1, "Your library, most played first -- Enter to pick, Esc to search instead, q to quit");
            widgets_draw_library(3, &state->library, state->library_cursor);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_LOADING_GAME:
            widgets_draw_header(1, "Loading...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c Fetching game info and reading player reviews...", spinner_ch);
            break;

        case TUI_SCREEN_LIKED:
        {
            widgets_draw_header(1, state->adding_item ? "Type a new liked item, Enter to add, Esc to cancel"
                                                        : "What did YOU like? Space toggles, (n) add your own, (m) move to disliked, Enter continues, Esc back, q quits");
            widgets_draw_checklist(3, state->themes.liked, state->themes.n_liked, state->liked_cursor);
            if (state->adding_item)
            {
                int width = tb_width() - 4;
                if (width < 1) width = 1;
                int y = 3 + (int)state->themes.n_liked + 1;
                tb_print(2, y, TB_DEFAULT, TB_DEFAULT, "New item >");
                widgets_draw_text_area(y + 1, width, state->custom_item_input, state->custom_item_input_cursor);
            }
            break;
        }

        case TUI_SCREEN_DISLIKED:
        {
            widgets_draw_header(1, state->adding_item ? "Type a new disliked item, Enter to add, Esc to cancel"
                                                        : "What bothered you? Space toggles, (n) add your own, (m) move to liked, Enter continues, Esc back, q quits");
            widgets_draw_checklist(3, state->themes.disliked, state->themes.n_disliked, state->disliked_cursor);
            if (state->adding_item)
            {
                int width = tb_width() - 4;
                if (width < 1) width = 1;
                int y = 3 + (int)state->themes.n_disliked + 1;
                tb_print(2, y, TB_DEFAULT, TB_DEFAULT, "New item >");
                widgets_draw_text_area(y + 1, width, state->custom_item_input, state->custom_item_input_cursor);
            }
            break;
        }

        case TUI_SCREEN_LOADING_FOLLOWUP:
            widgets_draw_header(1, "Thinking of a few questions...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c Coming up with a few optional follow-up questions...", spinner_ch);
            break;

        case TUI_SCREEN_LOADING_SPOILER_PREDICT:
            widgets_draw_header(1, "Checking for spoilers...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c Guessing whether any of your notes sound like a spoiler...", spinner_ch);
            break;

        case TUI_SCREEN_NOTES:
        {
            int width = tb_width() - 4;
            if (width < 1) width = 1;

            if (state->notes_step == TUI_NOTES_STEP_FOLLOWUP)
            {
                widgets_draw_header(1, "A few optional questions -- Enter to answer (blank skips), Esc back, q quits");

                char qline[1024];
                snprintf(qline, sizeof(qline), "(%zu/%zu) %s", state->followup_index + 1, state->n_followup_questions, state->followup_questions[state->followup_index]);
                size_t qrows = widgets_draw_text_area(3, width, qline, strlen(qline));

                int y = 3 + (int)qrows;
                tb_print(2, y, TB_DEFAULT, TB_DEFAULT, "Answer (blank skips) >");
                widgets_draw_text_area(y + 1, width, state->followup_answer[state->followup_index], state->followup_answer_cursor[state->followup_index]);
                break;
            }

            widgets_draw_header(1, "A few more questions -- Enter for a new line once you've typed something, Ctrl+S to continue");
            tb_print(2, 3, TB_DEFAULT, TB_DEFAULT, "Anything else?");

            size_t notes_rows = widgets_draw_text_area(4, width, state->notes_input, state->notes_input_cursor);
            int next_y = 4 + (int)notes_rows;

            if (state->notes_step != TUI_NOTES_STEP_TEXT) tb_hide_cursor();

            if (state->notes_step >= TUI_NOTES_STEP_RECOMMEND)
            {
                const char *answer = (state->notes_step == TUI_NOTES_STEP_RECOMMEND) ? "" : (state->recommend ? "y" : "n");
                tb_printf(2, next_y, TB_DEFAULT, TB_DEFAULT, "Recommend it? (y/n) > %s", answer);
            }

            if (state->notes_step == TUI_NOTES_STEP_RATING)
            {
                const char *prefix = "Rating 1-10, optional (Enter skips) > ";
                tb_printf(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "%s%s", prefix, state->rating_input);
                tb_set_cursor(2 + (int)strlen(prefix) + (int)state->rating_input_cursor, next_y + 1);
            }

            if (state->notes_step == TUI_NOTES_STEP_LENGTH)
            {
                tb_print(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "Length: (s)hort / (m)edium / (l)ong >");
            }

            if (state->notes_step == TUI_NOTES_STEP_VOCAB)
            {
                tb_print(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "Words: (n)ormal / (s)imple (simpler, everyday words) >");
            }

            if (state->notes_step == TUI_NOTES_STEP_FORMAT)
            {
                tb_print(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "Format: (p)lain / (b)bcode >");
            }

            if (state->notes_step == TUI_NOTES_STEP_SPOILER)
            {
                tb_print(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "Spoiler details (optional) -- Enter skips once blank, Ctrl+S continues");
                widgets_draw_text_area(next_y + 2, width, state->spoiler_input, state->spoiler_input_cursor);
            }
            break;
        }

        case TUI_SCREEN_LOADING_REVIEW:
            widgets_draw_header(1, "Writing your review...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c This can take a while on a local model -- Ctrl+C to give up", spinner_ch);
            break;

        case TUI_SCREEN_RESULT:
            widgets_draw_header(1, "Your review -- (r) regenerate, (e) edit, (c) copy, (s) save, (o) change answers, (n) pick another game, (q) quit");
            if (state->review_text) widgets_draw_wrapped_text(state->arena, 2, 3, tb_width() - 4, state->review_text);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_LOADING_TEST_CONNECTION:
            widgets_draw_header(1, "Testing connection...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c Trying provider/model/base_url as currently entered (not yet saved)...", spinner_ch);
            break;

        case TUI_SCREEN_SETTINGS:
        {
            widgets_draw_header(1, "Settings -- j/k fields, h/l or Enter cycles choices, Enter edits text, Ctrl+T tests, Ctrl+S saves, Esc cancels");

            static const char *length_names[] = {"short", "medium", "long"};
            static const char *tone_names[] = {"casual", "neutral", "enthusiastic"};
            static const char *vocab_names[] = {"normal", "simple"};

            draw_settings_field(3, "Provider:", SETTINGS_PROVIDER_NAMES[state->settings_provider_index], 0, state->settings_field == TUI_SETTINGS_FIELD_PROVIDER, false, false);
            draw_settings_field(4, "Model:", state->settings_model, state->settings_model_cursor, state->settings_field == TUI_SETTINGS_FIELD_MODEL, state->settings_editing, false);
            draw_settings_field(5, "Base URL:", state->settings_base_url, state->settings_base_url_cursor, state->settings_field == TUI_SETTINGS_FIELD_BASE_URL, state->settings_editing, false);
            draw_settings_field(6, "Num ctx:", state->settings_num_ctx, state->settings_num_ctx_cursor, state->settings_field == TUI_SETTINGS_FIELD_NUM_CTX, state->settings_editing, false);
            draw_settings_field(7, "Reviews:", state->settings_review_count, state->settings_review_count_cursor, state->settings_field == TUI_SETTINGS_FIELD_REVIEW_COUNT, state->settings_editing, false);
            draw_settings_field(8, "Language:", state->settings_steam_language, state->settings_steam_language_cursor, state->settings_field == TUI_SETTINGS_FIELD_STEAM_LANGUAGE, state->settings_editing, false);
            draw_settings_field(9, "Cache TTL:", state->settings_steam_cache_ttl_hours, state->settings_steam_cache_ttl_hours_cursor, state->settings_field == TUI_SETTINGS_FIELD_STEAM_CACHE_TTL_HOURS, state->settings_editing, false);
            draw_settings_field(10, "Steam ID:", state->settings_steam_id, state->settings_steam_id_cursor, state->settings_field == TUI_SETTINGS_FIELD_STEAM_ID, state->settings_editing, false);
            draw_settings_field(11, "Steam key:", state->settings_steam_api_key, state->settings_steam_api_key_cursor, state->settings_field == TUI_SETTINGS_FIELD_STEAM_API_KEY, state->settings_editing, true);
            draw_settings_field(12, "Length:", length_names[state->settings_review_length_index], 0, state->settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_LENGTH, false, false);
            draw_settings_field(13, "Tone:", tone_names[state->settings_review_tone_index], 0, state->settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_TONE, false, false);
            draw_settings_field(14, "Vocab:", vocab_names[state->settings_review_vocab_index], 0, state->settings_field == TUI_SETTINGS_FIELD_REVIEW_DEFAULT_VOCAB, false, false);

            tb_print(2, 16, TB_DEFAULT, TB_DEFAULT, "Provider/model/key setup for all three: see docs/PROVIDERS.md. Ctrl+T tests the values above.");
            tb_print(2, 17, TB_DEFAULT, TB_DEFAULT, "Base URL: empty uses the provider's default. Num ctx: ollama only.");
            tb_print(2, 18, TB_DEFAULT, TB_DEFAULT, "Reviews: how many player reviews to sample for theme extraction.");
            tb_print(2, 19, TB_DEFAULT, TB_DEFAULT, "Steam ID/key: your own SteamID64 + a Steam Web API key -- enables playtime in reviews.");
            tb_print(2, 20, TB_DEFAULT, TB_DEFAULT, "Steam key is encrypted at rest once saved. Anthropic/OpenAI keys stay env-only, not here.");
            if (state->cfg->steam_api_key_from_env) tb_print(2, 21, TB_DEFAULT, TB_DEFAULT, "Steam key is currently set via STEAM_API_KEY in the environment (takes priority over this).");
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
    steam_library_t library;
    game_info_t game;
    theme_set_t themes;
    char **questions;
    size_t n_questions;
    char *spoiler_guess;
    char *review_text;
    char *test_connection_reply;
} bg_result_t;

typedef struct bg_ctx_t
{
    app_state_t *state;
    const config_t *cfg;
    bg_result_t *result;
} bg_ctx_t;

static void *bg_fetch_library_thread(void *argp)
{
    bg_ctx_t *ctx = (bg_ctx_t *)argp;
    bg_result_t *result = ctx->result;

    err_t err;
    err_clear(&err);

    steam_library_t library;
    memset(&library, 0, sizeof(library));
    int rc = pipeline_fetch_library(ctx->state->arena, ctx->cfg, &library, &err);

    pthread_mutex_lock(&result->mutex);
    result->rc = rc;
    result->err = err;
    if (rc == 0) result->library = library;
    result->done = true;
    pthread_mutex_unlock(&result->mutex);

    free(ctx);
    return NULL;
}

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

static void *bg_generate_followup_thread(void *argp)
{
    bg_ctx_t *ctx = (bg_ctx_t *)argp;
    bg_result_t *result = ctx->result;

    err_t err;
    err_clear(&err);

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    int rc = pipeline_resolve_llm(ctx->cfg, &provider, &llm_cfg, &err);

    char **questions = NULL;
    size_t n_questions = 0;
    if (rc == 0) rc = followup_generate(ctx->state->arena, provider, &llm_cfg, &ctx->state->game, &ctx->state->themes, &questions, &n_questions, &err);

    pthread_mutex_lock(&result->mutex);
    result->rc = rc;
    result->err = err;
    if (rc == 0)
    {
        result->questions = questions;
        result->n_questions = n_questions;
    }
    result->done = true;
    pthread_mutex_unlock(&result->mutex);

    free(ctx);
    return NULL;
}

static void *bg_predict_spoiler_thread(void *argp)
{
    bg_ctx_t *ctx = (bg_ctx_t *)argp;
    bg_result_t *result = ctx->result;

    err_t err;
    err_clear(&err);

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    int rc = pipeline_resolve_llm(ctx->cfg, &provider, &llm_cfg, &err);

    char *spoiler_guess = NULL;
    if (rc == 0)
    {
        const char *answers[FOLLOWUP_MAX_QUESTIONS];
        for (size_t i = 0; i < ctx->state->n_followup_questions; i++) answers[i] = ctx->state->followup_answer[i];

        rc = spoiler_predict_generate(ctx->state->arena, provider, &llm_cfg, ctx->state->game.name, ctx->state->notes_input, answers, ctx->state->n_followup_questions, &spoiler_guess, &err);
    }

    pthread_mutex_lock(&result->mutex);
    result->rc = rc;
    result->err = err;
    if (rc == 0) result->spoiler_guess = spoiler_guess;
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
        char *followup_answer_ptrs[FOLLOWUP_MAX_QUESTIONS];
        for (size_t i = 0; i < ctx->state->n_followup_questions; i++) followup_answer_ptrs[i] = ctx->state->followup_answer[i];

        review_request_t req;
        req.game = &ctx->state->game;
        req.themes = &ctx->state->themes;
        req.notes = ctx->state->notes_input;
        req.recommend = ctx->state->recommend;
        req.rating = ctx->state->rating;
        req.length = ctx->state->length;
        req.tone = ctx->state->tone;
        req.vocab = ctx->state->vocab;
        req.followup_questions = ctx->state->followup_questions;
        req.followup_answers = ctx->state->n_followup_questions > 0 ? followup_answer_ptrs : NULL;
        req.n_followup = ctx->state->n_followup_questions;
        req.bbcode = ctx->state->bbcode;
        req.spoiler_notes = ctx->state->spoiler_input;

        steam_playtime_t playtime;
        pipeline_fetch_playtime(ctx->cfg, ctx->state->appid, &playtime);
        req.playtime_hours = playtime.found ? playtime.playtime_minutes / 60 : 0;

        rc = review_generate(ctx->state->arena, provider, &llm_cfg, &req, &review_text, &err);
        if (rc == 0) pipeline_unload_llm_model(provider, &llm_cfg);
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

/* Tests the Settings screen's *staged* provider/model/base_url/num_ctx
 * (not whatever's already saved to ctx->cfg) with one real, minimal
 * completion call -- so a typo'd model name or an unreachable base_url
 * shows up right there in Settings, before Ctrl+S commits it and it only
 * surfaces later as a failed review. */
static void *bg_test_connection_thread(void *argp)
{
    bg_ctx_t *ctx = (bg_ctx_t *)argp;
    bg_result_t *result = ctx->result;

    err_t err;
    err_clear(&err);

    const char *provider_name = SETTINGS_PROVIDER_NAMES[ctx->state->settings_provider_index];
    int num_ctx = ctx->state->settings_num_ctx_len > 0 ? atoi(ctx->state->settings_num_ctx) : 0;

    char *reply = NULL;
    int rc = pipeline_test_llm_connection(ctx->state->arena, ctx->cfg, provider_name, ctx->state->settings_model, ctx->state->settings_base_url, num_ctx, &reply, &err);

    pthread_mutex_lock(&result->mutex);
    result->rc = rc;
    result->err = err;
    if (rc == 0) result->test_connection_reply = reply;
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

static char *base64_encode(const char *data, size_t len)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    while (i < len)
    {
        uint32_t octet_a = i < len ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < len ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < len ? (unsigned char)data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = table[(triple >> 18) & 0x3F];
        out[j++] = table[(triple >> 12) & 0x3F];
        out[j++] = table[(triple >> 6) & 0x3F];
        out[j++] = table[triple & 0x3F];
    }

    size_t mod = len % 3;
    if (mod == 1) out[out_len - 1] = out[out_len - 2] = '=';
    else if (mod == 2) out[out_len - 1] = '=';

    out[out_len] = '\0';
    return out;
}

static bool try_copy_via(const char *cmd, const char *text, size_t len)
{
    FILE *fp = popen(cmd, "w");
    if (!fp) return false;
    fwrite(text, 1, len, fp);
    return pclose(fp) == 0;
}

/* Copies state->review_text to the clipboard two ways at once, since
 * there's no reliable way to tell from in here whether either one
 * actually worked: an OSC 52 escape sequence (no external dependency,
 * works in most modern terminals even over SSH/tmux) written straight to
 * stdout, and -- belt and suspenders -- whichever of wl-copy/xclip/pbcopy
 * is actually installed, via a subprocess fed the text on stdin. */
static void do_copy_review(app_state_t *state)
{
    if (!state->review_text || !state->review_text[0])
    {
        snprintf(state->status, sizeof(state->status), "Nothing to copy yet");
        return;
    }

    size_t len = strlen(state->review_text);

    char *b64 = base64_encode(state->review_text, len);
    if (b64)
    {
        fprintf(stdout, "\x1b]52;c;%s\x07", b64);
        fflush(stdout);
        free(b64);
    }

    bool copied = try_copy_via("wl-copy 2>/dev/null", state->review_text, len) || try_copy_via("xclip -selection clipboard 2>/dev/null", state->review_text, len) ||
                  try_copy_via("pbcopy 2>/dev/null", state->review_text, len);

    if (copied) snprintf(state->status, sizeof(state->status), "Copied to clipboard");
    else snprintf(state->status, sizeof(state->status), "Sent to clipboard via your terminal (OSC 52) -- no local clipboard tool found to confirm it landed");
}

/* Writes state->review_text to ~/.local/share/steamscribe/reviews/ (or
 * wherever XDG_DATA_HOME points), named "<appid>-<date>.txt" per
 * DESIGN.md 7 -- the appid and timestamp are enough on their own to make
 * a unique, sortable filename without needing to sanitize the game name. */
static void do_save_review(app_state_t *state)
{
    if (!state->review_text || !state->review_text[0])
    {
        snprintf(state->status, sizeof(state->status), "Nothing to save yet");
        return;
    }

    char *data_dir = goblin_appdir_data_dir("steamscribe");
    if (!data_dir)
    {
        snprintf(state->status, sizeof(state->status), "Could not save: no home directory found");
        return;
    }

    char *reviews_dir = goblin_appdir_join(data_dir, "reviews");
    free(data_dir);
    if (!reviews_dir)
    {
        snprintf(state->status, sizeof(state->status), "Could not save: out of memory");
        return;
    }

    if (goblin_appdir_mkdir_p(reviews_dir) != 0)
    {
        snprintf(state->status, sizeof(state->status), "Could not save: could not create %s", reviews_dir);
        free(reviews_dir);
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_now);

    char filename[64];
    snprintf(filename, sizeof(filename), "%u-%s.txt", state->appid, stamp);

    char *path = goblin_appdir_join(reviews_dir, filename);
    free(reviews_dir);
    if (!path)
    {
        snprintf(state->status, sizeof(state->status), "Could not save: out of memory");
        return;
    }

    FILE *fp = fopen(path, "w");
    if (!fp)
    {
        snprintf(state->status, sizeof(state->status), "Could not save: could not create %s", path);
        free(path);
        return;
    }

    fputs(state->review_text, fp);
    fclose(fp);

    snprintf(state->status, sizeof(state->status), "Saved to %s", path);
    free(path);
}

/* Opens state->review_text in $EDITOR (falling back to "vi"), replacing it
 * with whatever's in the file afterward. termbox2 has to release the
 * terminal first -- the editor needs it, same as any other foreground
 * program would -- and re-init it once the editor exits. Leaves
 * review_text untouched on any failure. */
static void do_edit_review(app_state_t *state)
{
    char path[] = "/tmp/steamscribe-review-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
    {
        snprintf(state->status, sizeof(state->status), "Could not edit: could not create a temp file");
        return;
    }

    FILE *fp = fdopen(fd, "w");
    if (fp)
    {
        if (state->review_text) fputs(state->review_text, fp);
        fclose(fp);
    }
    else
    {
        close(fd);
    }

    const char *editor = getenv("EDITOR");
    if (!editor || !editor[0]) editor = "vi";

    char cmd[768];
    snprintf(cmd, sizeof(cmd), "%s '%s'", editor, path);

    tb_shutdown();
    int rc = system(cmd);
    if (tb_init() != 0)
    {
        /* Nothing can be safely drawn without a working terminal. */
        unlink(path);
        _exit(1);
    }

    if (rc != 0)
    {
        snprintf(state->status, sizeof(state->status), "$EDITOR exited with an error -- review left unchanged");
        unlink(path);
        return;
    }

    fp = fopen(path, "r");
    if (!fp)
    {
        snprintf(state->status, sizeof(state->status), "Could not read back the edited review");
        unlink(path);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0)
    {
        fclose(fp);
        unlink(path);
        return;
    }

    char *buf = (char *)arena_alloc(state->arena, (size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, fp);
    buf[n] = '\0';
    fclose(fp);
    unlink(path);

    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) buf[--n] = '\0';

    state->review_text = buf;
    state->status[0] = '\0';
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
            flush_pending_status(state);
            _exit(130);
        }
    }

    pthread_join(thread, NULL);
    pthread_mutex_destroy(&result.mutex);

    if (state->screen == TUI_SCREEN_LOADING_LIBRARY)
    {
        if (result.rc == 0) tui_apply_library_loaded(state, result.library);
        else tui_apply_library_load_failed(state, result.err.msg);
    }
    else if (state->screen == TUI_SCREEN_LOADING_GAME)
    {
        if (result.rc == 0) tui_apply_game_loaded(state, state->appid, result.game, result.themes);
        else tui_apply_game_load_failed(state, result.err.msg);
    }
    else if (state->screen == TUI_SCREEN_LOADING_FOLLOWUP)
    {
        /* Best-effort and optional: a failure here just means zero
         * questions, never a hard error shown to the user. */
        if (result.rc == 0) tui_apply_followup_generated(state, result.questions, result.n_questions);
        else tui_apply_followup_failed(state);
    }
    else if (state->screen == TUI_SCREEN_LOADING_SPOILER_PREDICT)
    {
        /* Best-effort and optional: a failure here just means the Spoiler
         * step starts blank, never a hard error shown to the user. */
        if (result.rc == 0) tui_apply_spoiler_predicted(state, result.spoiler_guess);
        else tui_apply_spoiler_predict_failed(state);
    }
    else if (state->screen == TUI_SCREEN_LOADING_TEST_CONNECTION)
    {
        if (result.rc == 0) tui_apply_test_connection_ok(state, result.test_connection_reply);
        else tui_apply_test_connection_failed(state, result.err.msg);
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
    g_signal_state = &state;

    while (state.screen != TUI_SCREEN_QUIT)
    {
        if (is_loading_screen(state.screen))
        {
            tui_screen_t loading_screen = state.screen;
            void *(*thread_fn)(void *) = bg_generate_review_thread;
            if (loading_screen == TUI_SCREEN_LOADING_LIBRARY) thread_fn = bg_fetch_library_thread;
            else if (loading_screen == TUI_SCREEN_LOADING_GAME) thread_fn = bg_load_game_thread;
            else if (loading_screen == TUI_SCREEN_LOADING_FOLLOWUP) thread_fn = bg_generate_followup_thread;
            else if (loading_screen == TUI_SCREEN_LOADING_SPOILER_PREDICT) thread_fn = bg_predict_spoiler_thread;
            else if (loading_screen == TUI_SCREEN_LOADING_TEST_CONNECTION) thread_fn = bg_test_connection_thread;

            run_loading_screen(&state, cfg, thread_fn);
            continue;
        }

        draw(&state, ' ');

        struct tb_event ev;
        if (tb_poll_event(&ev) != TB_OK) continue;
        if (ev.type != TB_EVENT_KEY) continue; /* TB_EVENT_RESIZE: next loop redraws at the new size */

        tui_action_t action = tui_handle_key(&state, ev.ch, ev.key);
        if (action == TUI_ACTION_SEARCH) do_search(&state, cfg);
        else if (action == TUI_ACTION_SAVE_SETTINGS) do_save_settings(&state);
        else if (action == TUI_ACTION_EDIT_REVIEW) do_edit_review(&state);
        else if (action == TUI_ACTION_COPY_REVIEW) do_copy_review(&state);
        else if (action == TUI_ACTION_SAVE_REVIEW) do_save_review(&state);
    }

    tb_shutdown();
    g_signal_state = NULL;
    flush_pending_status(&state);
    return 0;
}

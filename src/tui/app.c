#include "tui/app.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <termbox2.h>

#include "core/followup.h"
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
    return screen == TUI_SCREEN_LOADING_GAME || screen == TUI_SCREEN_LOADING_FOLLOWUP || screen == TUI_SCREEN_LOADING_REVIEW;
}

static void draw_settings_field(int y, const char *label, const char *value, size_t cursor, bool focused, bool editing)
{
    uintattr_t fg = focused ? TB_BLACK : TB_DEFAULT;
    uintattr_t bg = focused ? TB_CYAN : TB_DEFAULT;
    tb_printf(2, y, fg, bg, "%-10s %s", label, value);
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

            widgets_draw_header(1, "steamscribe -- search for a game (F2 for settings)");
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

            if (state->notes_step == TUI_NOTES_STEP_LENGTH)
            {
                tb_print(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "Length: (s)hort / (m)edium / (l)ong >");
            }

            if (state->notes_step == TUI_NOTES_STEP_VOCAB)
            {
                tb_print(2, next_y + 1, TB_DEFAULT, TB_DEFAULT, "Words: (n)ormal / (s)imple (simpler, everyday words) >");
            }
            break;
        }

        case TUI_SCREEN_LOADING_REVIEW:
            widgets_draw_header(1, "Writing your review...");
            tb_printf(2, 3, TB_DEFAULT, TB_DEFAULT, "%c This can take a while on a local model -- Ctrl+C to give up", spinner_ch);
            break;

        case TUI_SCREEN_RESULT:
            widgets_draw_header(1, "Your review -- (r) regenerate, (e) edit in $EDITOR, (c) copy, (o) change your answers, (q) quit");
            if (state->review_text) widgets_draw_wrapped_text(state->arena, 2, 3, tb_width() - 4, state->review_text);
            widgets_draw_status_bar(state->arena, state->status);
            break;

        case TUI_SCREEN_SETTINGS:
        {
            widgets_draw_header(1, "Settings -- j/k fields, h/l or Enter cycles provider, Enter edits, Ctrl+S saves, Esc cancels");

            static const char *provider_names[] = {"anthropic", "openai", "ollama"};
            draw_settings_field(3, "Provider:", provider_names[state->settings_provider_index], 0, state->settings_field == TUI_SETTINGS_FIELD_PROVIDER, false);
            draw_settings_field(4, "Model:", state->settings_model, state->settings_model_cursor, state->settings_field == TUI_SETTINGS_FIELD_MODEL, state->settings_editing);
            draw_settings_field(5, "Base URL:", state->settings_base_url, state->settings_base_url_cursor, state->settings_field == TUI_SETTINGS_FIELD_BASE_URL, state->settings_editing);
            draw_settings_field(6, "Num ctx:", state->settings_num_ctx, state->settings_num_ctx_cursor, state->settings_field == TUI_SETTINGS_FIELD_NUM_CTX, state->settings_editing);
            draw_settings_field(7, "Reviews:", state->settings_review_count, state->settings_review_count_cursor, state->settings_field == TUI_SETTINGS_FIELD_REVIEW_COUNT, state->settings_editing);

            tb_print(2, 9, TB_DEFAULT, TB_DEFAULT, "Base URL: empty uses the provider's default. Num ctx: ollama only.");
            tb_print(2, 10, TB_DEFAULT, TB_DEFAULT, "Reviews: how many player reviews to sample for theme extraction.");
            tb_print(2, 11, TB_DEFAULT, TB_DEFAULT, "API keys come only from ANTHROPIC_API_KEY / OPENAI_API_KEY -- not editable here.");
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
    char **questions;
    size_t n_questions;
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
        req.length = ctx->state->length;
        req.tone = ctx->state->tone;
        req.vocab = ctx->state->vocab;
        req.followup_questions = ctx->state->followup_questions;
        req.followup_answers = ctx->state->n_followup_questions > 0 ? followup_answer_ptrs : NULL;
        req.n_followup = ctx->state->n_followup_questions;

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
    else if (state->screen == TUI_SCREEN_LOADING_FOLLOWUP)
    {
        /* Best-effort and optional: a failure here just means zero
         * questions, never a hard error shown to the user. */
        if (result.rc == 0) tui_apply_followup_generated(state, result.questions, result.n_questions);
        else tui_apply_followup_failed(state);
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
            void *(*thread_fn)(void *) = bg_generate_review_thread;
            if (loading_screen == TUI_SCREEN_LOADING_GAME) thread_fn = bg_load_game_thread;
            else if (loading_screen == TUI_SCREEN_LOADING_FOLLOWUP) thread_fn = bg_generate_followup_thread;

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
    }

    tb_shutdown();
    return 0;
}

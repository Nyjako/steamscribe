#include "tui/state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tui/keys.h"

static const char *SETTINGS_PROVIDERS[] = {"anthropic", "openai", "ollama"};
#define SETTINGS_PROVIDER_COUNT (sizeof(SETTINGS_PROVIDERS) / sizeof(SETTINGS_PROVIDERS[0]))

static const char *SETTINGS_REVIEW_LENGTHS[] = {"short", "medium", "long"};
#define SETTINGS_REVIEW_LENGTH_COUNT (sizeof(SETTINGS_REVIEW_LENGTHS) / sizeof(SETTINGS_REVIEW_LENGTHS[0]))

static const char *SETTINGS_REVIEW_TONES[] = {"casual", "neutral", "enthusiastic"};
#define SETTINGS_REVIEW_TONE_COUNT (sizeof(SETTINGS_REVIEW_TONES) / sizeof(SETTINGS_REVIEW_TONES[0]))

static const char *SETTINGS_REVIEW_VOCABS[] = {"normal", "simple"};
#define SETTINGS_REVIEW_VOCAB_COUNT (sizeof(SETTINGS_REVIEW_VOCABS) / sizeof(SETTINGS_REVIEW_VOCABS[0]))

static review_tone_t tone_from_config(const config_t *cfg)
{
    if (strcmp(cfg->review_default_tone, "neutral") == 0) return REVIEW_TONE_NEUTRAL;
    if (strcmp(cfg->review_default_tone, "enthusiastic") == 0) return REVIEW_TONE_ENTHUSIASTIC;
    return REVIEW_TONE_CASUAL;
}

static review_vocab_t vocab_from_config(const config_t *cfg)
{
    if (strcmp(cfg->review_default_vocab, "simple") == 0) return REVIEW_VOCAB_SIMPLE;
    return REVIEW_VOCAB_NORMAL;
}

void tui_state_init(app_state_t *state, arena_t *arena, config_t *cfg, const char *initial_query, uint32_t initial_appid)
{
    memset(state, 0, sizeof(*state));
    state->arena = arena;
    state->cfg = cfg;
    state->screen = TUI_SCREEN_SEARCH;
    state->tone = tone_from_config(cfg);
    state->vocab = vocab_from_config(cfg);
    state->length = REVIEW_LEN_SHORT;

    if (initial_appid != 0)
    {
        state->appid = initial_appid;
        state->screen = TUI_SCREEN_LOADING_GAME;
        return;
    }

    if (initial_query && initial_query[0])
    {
        size_t len = strlen(initial_query);
        if (len >= sizeof(state->search_input)) len = sizeof(state->search_input) - 1;
        memcpy(state->search_input, initial_query, len);
        state->search_input[len] = '\0';
        state->search_input_len = len;
    }
}

static bool is_printable(uint32_t ch)
{
    return ch >= 0x20 && ch < 0x7f;
}

/* A text field bundled as (buffer, capacity, *length, *cursor) so the
 * editing primitives below -- and the one generic key handler built on
 * them -- work the same way for every text input in the TUI: search,
 * custom checklist items, notes, follow-up answers, and Settings fields.
 * `cursor` is a byte offset into `buf`, 0..*len; insert/delete act at it,
 * not just at the end, and the left/right/home/end keys move it. */
typedef struct text_field_t
{
    char *buf;
    size_t cap;
    size_t *len;
    size_t *cursor;
} text_field_t;

static void text_field_insert_char(text_field_t *f, char ch)
{
    if (*f->len + 1 >= f->cap) return;
    memmove(f->buf + *f->cursor + 1, f->buf + *f->cursor, *f->len - *f->cursor);
    f->buf[*f->cursor] = ch;
    (*f->len)++;
    (*f->cursor)++;
    f->buf[*f->len] = '\0';
}

static void text_field_backspace(text_field_t *f)
{
    if (*f->cursor == 0) return;
    memmove(f->buf + *f->cursor - 1, f->buf + *f->cursor, *f->len - *f->cursor);
    (*f->len)--;
    (*f->cursor)--;
    f->buf[*f->len] = '\0';
}

static void text_field_delete_forward(text_field_t *f)
{
    if (*f->cursor >= *f->len) return;
    memmove(f->buf + *f->cursor, f->buf + *f->cursor + 1, *f->len - *f->cursor - 1);
    (*f->len)--;
    f->buf[*f->len] = '\0';
}

/* Handles the editing keys every text field shares -- backspace, forward
 * delete, left/right/home/end, and inserting a typed character -- so each
 * field's own key handler only needs to deal with what's specific to it
 * (Enter, Esc, Ctrl+S, ...). Returns true if it consumed the key. */
static bool handle_text_field_key(text_field_t *f, uint32_t ch, uint16_t key, bool numeric_only)
{
    if (key == TUI_KEY_BACKSPACE || key == TUI_KEY_BACKSPACE2)
    {
        text_field_backspace(f);
        return true;
    }

    if (key == TUI_KEY_DELETE)
    {
        text_field_delete_forward(f);
        return true;
    }

    if (key == TUI_KEY_ARROW_LEFT)
    {
        if (*f->cursor > 0) (*f->cursor)--;
        return true;
    }

    if (key == TUI_KEY_ARROW_RIGHT)
    {
        if (*f->cursor < *f->len) (*f->cursor)++;
        return true;
    }

    if (key == TUI_KEY_HOME)
    {
        *f->cursor = 0;
        return true;
    }

    if (key == TUI_KEY_END)
    {
        *f->cursor = *f->len;
        return true;
    }

    bool ok_char = numeric_only ? (ch >= '0' && ch <= '9') : is_printable(ch);
    if (ok_char)
    {
        text_field_insert_char(f, (char)ch);
        return true;
    }

    return false;
}

static tui_action_t handle_search_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (key == TUI_KEY_ENTER)
    {
        if (state->search_input_len == 0) return TUI_ACTION_NONE;
        return TUI_ACTION_SEARCH;
    }

    text_field_t f = {state->search_input, sizeof(state->search_input), &state->search_input_len, &state->search_input_cursor};
    handle_text_field_key(&f, ch, key, false);
    return TUI_ACTION_NONE;
}

static tui_action_t handle_select_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (ch == 'q')
    {
        state->screen = TUI_SCREEN_QUIT;
        return TUI_ACTION_QUIT;
    }

    if (key == TUI_KEY_ESC)
    {
        state->screen = TUI_SCREEN_SEARCH;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_UP || ch == 'k') && state->select_cursor > 0)
    {
        state->select_cursor--;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_DOWN || ch == 'j') && state->select_cursor + 1 < state->search_results.count)
    {
        state->select_cursor++;
        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_ENTER)
    {
        if (state->search_results.count == 0) return TUI_ACTION_NONE;
        state->appid = state->search_results.items[state->select_cursor].appid;
        state->screen = TUI_SCREEN_LOADING_GAME;
        return TUI_ACTION_LOAD_GAME;
    }

    return TUI_ACTION_NONE;
}

static tui_action_t handle_library_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (ch == 'q')
    {
        state->screen = TUI_SCREEN_QUIT;
        return TUI_ACTION_QUIT;
    }

    if (key == TUI_KEY_ESC)
    {
        state->screen = TUI_SCREEN_SEARCH;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_UP || ch == 'k') && state->library_cursor > 0)
    {
        state->library_cursor--;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_DOWN || ch == 'j') && state->library_cursor + 1 < state->library.count)
    {
        state->library_cursor++;
        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_ENTER)
    {
        if (state->library.count == 0) return TUI_ACTION_NONE;
        state->appid = state->library.items[state->library_cursor].appid;
        state->screen = TUI_SCREEN_LOADING_GAME;
        return TUI_ACTION_LOAD_GAME;
    }

    return TUI_ACTION_NONE;
}

/* Resets the "add a custom item" text buffer, whether entering or leaving
 * that sub-mode. */
static void adding_item_reset(app_state_t *state)
{
    state->adding_item = false;
    state->custom_item_input_len = 0;
    state->custom_item_input_cursor = 0;
    state->custom_item_input[0] = '\0';
}

static tui_action_t handle_adding_item_key(app_state_t *state, uint32_t ch, uint16_t key, bool is_liked_side, size_t *cursor)
{
    if (key == TUI_KEY_ESC)
    {
        adding_item_reset(state);
        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_ENTER)
    {
        if (state->custom_item_input_len > 0)
        {
            if (is_liked_side) themes_add_custom_item(state->arena, &state->themes.liked, &state->themes.n_liked, state->custom_item_input);
            else themes_add_custom_item(state->arena, &state->themes.disliked, &state->themes.n_disliked, state->custom_item_input);

            *cursor = (is_liked_side ? state->themes.n_liked : state->themes.n_disliked) - 1;
        }

        adding_item_reset(state);
        return TUI_ACTION_NONE;
    }

    text_field_t f = {state->custom_item_input, sizeof(state->custom_item_input), &state->custom_item_input_len, &state->custom_item_input_cursor};
    handle_text_field_key(&f, ch, key, false);
    return TUI_ACTION_NONE;
}

static tui_action_t handle_checklist_key(app_state_t *state, uint32_t ch, uint16_t key, bool is_liked_side, tui_screen_t next_screen, tui_screen_t back_screen)
{
    theme_t *items = is_liked_side ? state->themes.liked : state->themes.disliked;
    size_t count = is_liked_side ? state->themes.n_liked : state->themes.n_disliked;
    size_t *cursor = is_liked_side ? &state->liked_cursor : &state->disliked_cursor;

    if (state->adding_item) return handle_adding_item_key(state, ch, key, is_liked_side, cursor);

    if (ch == 'q')
    {
        state->screen = TUI_SCREEN_QUIT;
        return TUI_ACTION_QUIT;
    }

    if (key == TUI_KEY_ESC)
    {
        state->screen = back_screen;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_UP || ch == 'k') && *cursor > 0)
    {
        (*cursor)--;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_DOWN || ch == 'j') && count > 0 && *cursor + 1 < count)
    {
        (*cursor)++;
        return TUI_ACTION_NONE;
    }

    if (ch == ' ' && count > 0)
    {
        items[*cursor].checked = !items[*cursor].checked;
        return TUI_ACTION_NONE;
    }

    if (ch == 'n')
    {
        state->adding_item = true;
        return TUI_ACTION_NONE;
    }

    if (ch == 'm' && count > 0)
    {
        if (is_liked_side) themes_move_item(state->arena, &state->themes.liked, &state->themes.n_liked, &state->themes.disliked, &state->themes.n_disliked, *cursor);
        else themes_move_item(state->arena, &state->themes.disliked, &state->themes.n_disliked, &state->themes.liked, &state->themes.n_liked, *cursor);

        size_t new_count = is_liked_side ? state->themes.n_liked : state->themes.n_disliked;
        if (new_count == 0) *cursor = 0;
        else if (*cursor >= new_count) *cursor = new_count - 1;

        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_ENTER)
    {
        if (!is_liked_side)
        {
            /* After Disliked, ask the LLM for a few optional follow-up
             * questions tailored to what's actually checked, before the
             * free-text notes step. Best-effort -- a failure here just
             * means zero questions, handled in tui_apply_followup_failed. */
            state->screen = TUI_SCREEN_LOADING_FOLLOWUP;
            return TUI_ACTION_GENERATE_FOLLOWUP;
        }

        state->screen = next_screen;
        return TUI_ACTION_NONE;
    }

    return TUI_ACTION_NONE;
}

static tui_action_t handle_notes_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    switch (state->notes_step)
    {
        case TUI_NOTES_STEP_FOLLOWUP:
            if (key == TUI_KEY_ESC)
            {
                if (state->followup_index > 0) state->followup_index--;
                else state->screen = TUI_SCREEN_DISLIKED;
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_ENTER)
            {
                state->followup_index++;
                if (state->followup_index >= state->n_followup_questions) state->notes_step = TUI_NOTES_STEP_TEXT;
                return TUI_ACTION_NONE;
            }
            {
                text_field_t f = {state->followup_answer[state->followup_index], sizeof(state->followup_answer[0]),
                                   &state->followup_answer_len[state->followup_index], &state->followup_answer_cursor[state->followup_index]};
                handle_text_field_key(&f, ch, key, false);
            }
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_TEXT:
            if (key == TUI_KEY_ESC)
            {
                if (state->n_followup_questions > 0)
                {
                    state->followup_index = state->n_followup_questions - 1;
                    state->notes_step = TUI_NOTES_STEP_FOLLOWUP;
                }
                else
                {
                    state->screen = TUI_SCREEN_DISLIKED;
                }
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_CTRL_S)
            {
                state->notes_step = TUI_NOTES_STEP_RECOMMEND;
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_ENTER)
            {
                /* Nothing typed yet -- Enter is a quick "skip this, I have
                 * nothing to add" rather than a newline in an empty field.
                 * Once there's content, Enter starts a new line instead;
                 * Ctrl+S (above) is what submits from then on. This also
                 * means a paste containing embedded newlines no longer
                 * blows straight through to generating a review -- each
                 * embedded Enter used to advance a whole step. */
                if (state->notes_input_len == 0)
                {
                    state->notes_step = TUI_NOTES_STEP_RECOMMEND;
                    return TUI_ACTION_NONE;
                }

                text_field_t f = {state->notes_input, sizeof(state->notes_input), &state->notes_input_len, &state->notes_input_cursor};
                text_field_insert_char(&f, '\n');
                return TUI_ACTION_NONE;
            }

            {
                text_field_t f = {state->notes_input, sizeof(state->notes_input), &state->notes_input_len, &state->notes_input_cursor};
                handle_text_field_key(&f, ch, key, false);
            }
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_RECOMMEND:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_TEXT;
                return TUI_ACTION_NONE;
            }
            if (ch == 'y' || ch == 'Y')
            {
                state->recommend = true;
                state->notes_step = TUI_NOTES_STEP_RATING;
            }
            else if (ch == 'n' || ch == 'N')
            {
                state->recommend = false;
                state->notes_step = TUI_NOTES_STEP_RATING;
            }
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_RATING:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_RECOMMEND;
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_ENTER)
            {
                /* Optional -- a blank field just means no rating given,
                 * same "Enter skips" convention as the notes text field. */
                int rating = state->rating_input_len > 0 ? atoi(state->rating_input) : 0;
                if (rating < 0) rating = 0;
                if (rating > 10) rating = 10;
                state->rating = rating;
                state->notes_step = TUI_NOTES_STEP_LENGTH;
                return TUI_ACTION_NONE;
            }
            {
                text_field_t f = {state->rating_input, sizeof(state->rating_input), &state->rating_input_len, &state->rating_input_cursor};
                handle_text_field_key(&f, ch, key, true);
            }
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_LENGTH:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_RATING;
                return TUI_ACTION_NONE;
            }
            if (ch == 's' || ch == 'S') state->length = REVIEW_LEN_SHORT;
            else if (ch == 'm' || ch == 'M') state->length = REVIEW_LEN_MEDIUM;
            else if (ch == 'l' || ch == 'L') state->length = REVIEW_LEN_LONG;
            else return TUI_ACTION_NONE;

            state->notes_step = TUI_NOTES_STEP_VOCAB;
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_VOCAB:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_LENGTH;
                return TUI_ACTION_NONE;
            }
            if (ch == 'n' || ch == 'N') state->vocab = REVIEW_VOCAB_NORMAL;
            else if (ch == 's' || ch == 'S') state->vocab = REVIEW_VOCAB_SIMPLE;
            else return TUI_ACTION_NONE;

            state->notes_step = TUI_NOTES_STEP_FORMAT;
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_FORMAT:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_VOCAB;
                return TUI_ACTION_NONE;
            }
            if (ch == 'p' || ch == 'P') state->bbcode = false;
            else if (ch == 'b' || ch == 'B') state->bbcode = true;
            else return TUI_ACTION_NONE;

            /* Best-guess the Spoiler step's pre-fill via one extra LLM call
             * -- only when nothing's there yet, so this never clobbers an
             * answer from a previous pass through (e.g. via "change
             * answers" from the Result screen), and never fires twice for
             * the same pass. The player still edits or clears whatever
             * comes back, the same as if they'd typed it themselves;
             * nothing ships without going through this field. */
            if (state->spoiler_input_len == 0)
            {
                state->screen = TUI_SCREEN_LOADING_SPOILER_PREDICT;
                return TUI_ACTION_PREDICT_SPOILER;
            }

            state->notes_step = TUI_NOTES_STEP_SPOILER;
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_SPOILER:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_FORMAT;
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_CTRL_S)
            {
                state->screen = TUI_SCREEN_LOADING_REVIEW;
                return TUI_ACTION_GENERATE_REVIEW;
            }
            if (key == TUI_KEY_ENTER)
            {
                /* Same "Enter skips an empty field, otherwise inserts a
                 * newline" convention as the main notes field. */
                if (state->spoiler_input_len == 0)
                {
                    state->screen = TUI_SCREEN_LOADING_REVIEW;
                    return TUI_ACTION_GENERATE_REVIEW;
                }

                text_field_t f = {state->spoiler_input, sizeof(state->spoiler_input), &state->spoiler_input_len, &state->spoiler_input_cursor};
                text_field_insert_char(&f, '\n');
                return TUI_ACTION_NONE;
            }

            {
                text_field_t f = {state->spoiler_input, sizeof(state->spoiler_input), &state->spoiler_input_len, &state->spoiler_input_cursor};
                handle_text_field_key(&f, ch, key, false);
            }
            return TUI_ACTION_NONE;

        default:
            return TUI_ACTION_NONE;
    }
}

/* Copies `src` into a staging buffer and puts the cursor at the end of it
 * -- the usual place to start editing existing text. */
static void copy_into(char *dst, size_t dst_cap, size_t *dst_len, size_t *dst_cursor, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_cap) len = dst_cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    *dst_len = len;
    *dst_cursor = len;
}

static int settings_index_of(const char *const *options, size_t count, const char *value)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(value, options[i]) == 0) return (int)i;
    }
    return 0;
}

static void settings_enter(app_state_t *state)
{
    state->settings_return_screen = state->screen;
    state->screen = TUI_SCREEN_SETTINGS;
    state->settings_field = TUI_SETTINGS_FIELD_PROVIDER;
    state->settings_editing = false;
    state->status[0] = '\0';

    state->settings_provider_index = settings_index_of(SETTINGS_PROVIDERS, SETTINGS_PROVIDER_COUNT, state->cfg->llm_provider);
    state->settings_review_length_index = settings_index_of(SETTINGS_REVIEW_LENGTHS, SETTINGS_REVIEW_LENGTH_COUNT, state->cfg->review_default_length);
    state->settings_review_tone_index = settings_index_of(SETTINGS_REVIEW_TONES, SETTINGS_REVIEW_TONE_COUNT, state->cfg->review_default_tone);
    state->settings_review_vocab_index = settings_index_of(SETTINGS_REVIEW_VOCABS, SETTINGS_REVIEW_VOCAB_COUNT, state->cfg->review_default_vocab);

    copy_into(state->settings_model, sizeof(state->settings_model), &state->settings_model_len, &state->settings_model_cursor, state->cfg->llm_model);
    copy_into(state->settings_base_url, sizeof(state->settings_base_url), &state->settings_base_url_len, &state->settings_base_url_cursor, state->cfg->llm_base_url);
    copy_into(state->settings_steam_language, sizeof(state->settings_steam_language), &state->settings_steam_language_len, &state->settings_steam_language_cursor, state->cfg->steam_language);
    copy_into(state->settings_steam_id, sizeof(state->settings_steam_id), &state->settings_steam_id_len, &state->settings_steam_id_cursor, state->cfg->steam_id);
    copy_into(state->settings_steam_api_key, sizeof(state->settings_steam_api_key), &state->settings_steam_api_key_len, &state->settings_steam_api_key_cursor, state->cfg->steam_api_key);

    char num_str[TUI_SETTINGS_NUM_CTX_CAP];
    snprintf(num_str, sizeof(num_str), "%d", state->cfg->llm_num_ctx);
    copy_into(state->settings_num_ctx, sizeof(state->settings_num_ctx), &state->settings_num_ctx_len, &state->settings_num_ctx_cursor, num_str);

    snprintf(num_str, sizeof(num_str), "%d", state->cfg->steam_review_count);
    copy_into(state->settings_review_count, sizeof(state->settings_review_count), &state->settings_review_count_len, &state->settings_review_count_cursor, num_str);

    char ttl_str[TUI_SETTINGS_CACHE_TTL_CAP];
    snprintf(ttl_str, sizeof(ttl_str), "%d", state->cfg->steam_cache_ttl_hours);
    copy_into(state->settings_steam_cache_ttl_hours, sizeof(state->settings_steam_cache_ttl_hours), &state->settings_steam_cache_ttl_hours_len, &state->settings_steam_cache_ttl_hours_cursor, ttl_str);
}

/* Reverts one field's edit buffer back to *cfg's value (Esc-while-editing). */
static void settings_reset_field(app_state_t *state, tui_settings_field_t field)
{
    char num_str[TUI_SETTINGS_NUM_CTX_CAP];

    switch (field)
    {
        case TUI_SETTINGS_FIELD_MODEL:
            copy_into(state->settings_model, sizeof(state->settings_model), &state->settings_model_len, &state->settings_model_cursor, state->cfg->llm_model);
            break;
        case TUI_SETTINGS_FIELD_BASE_URL:
            copy_into(state->settings_base_url, sizeof(state->settings_base_url), &state->settings_base_url_len, &state->settings_base_url_cursor, state->cfg->llm_base_url);
            break;
        case TUI_SETTINGS_FIELD_NUM_CTX:
            snprintf(num_str, sizeof(num_str), "%d", state->cfg->llm_num_ctx);
            copy_into(state->settings_num_ctx, sizeof(state->settings_num_ctx), &state->settings_num_ctx_len, &state->settings_num_ctx_cursor, num_str);
            break;
        case TUI_SETTINGS_FIELD_REVIEW_COUNT:
            snprintf(num_str, sizeof(num_str), "%d", state->cfg->steam_review_count);
            copy_into(state->settings_review_count, sizeof(state->settings_review_count), &state->settings_review_count_len, &state->settings_review_count_cursor, num_str);
            break;
        case TUI_SETTINGS_FIELD_STEAM_LANGUAGE:
            copy_into(state->settings_steam_language, sizeof(state->settings_steam_language), &state->settings_steam_language_len, &state->settings_steam_language_cursor, state->cfg->steam_language);
            break;
        case TUI_SETTINGS_FIELD_STEAM_CACHE_TTL_HOURS:
            snprintf(num_str, sizeof(num_str), "%d", state->cfg->steam_cache_ttl_hours);
            copy_into(state->settings_steam_cache_ttl_hours, sizeof(state->settings_steam_cache_ttl_hours), &state->settings_steam_cache_ttl_hours_len, &state->settings_steam_cache_ttl_hours_cursor, num_str);
            break;
        case TUI_SETTINGS_FIELD_STEAM_ID:
            copy_into(state->settings_steam_id, sizeof(state->settings_steam_id), &state->settings_steam_id_len, &state->settings_steam_id_cursor, state->cfg->steam_id);
            break;
        case TUI_SETTINGS_FIELD_STEAM_API_KEY:
            copy_into(state->settings_steam_api_key, sizeof(state->settings_steam_api_key), &state->settings_steam_api_key_len, &state->settings_steam_api_key_cursor, state->cfg->steam_api_key);
            break;
        default:
            break;
    }
}

/* Writes the staging buffers into *cfg (arena-allocated); the caller still
 * has to perform the actual config_save() IO. */
static void settings_commit_to_cfg(app_state_t *state)
{
    state->cfg->llm_provider = arena_strdup(state->arena, SETTINGS_PROVIDERS[state->settings_provider_index]);
    state->cfg->llm_model = arena_strdup(state->arena, state->settings_model);
    state->cfg->llm_base_url = arena_strdup(state->arena, state->settings_base_url);
    state->cfg->llm_num_ctx = state->settings_num_ctx_len > 0 ? atoi(state->settings_num_ctx) : 0;
    state->cfg->steam_review_count = state->settings_review_count_len > 0 ? atoi(state->settings_review_count) : state->cfg->steam_review_count;
    state->cfg->steam_language = arena_strdup(state->arena, state->settings_steam_language);
    state->cfg->steam_cache_ttl_hours = state->settings_steam_cache_ttl_hours_len > 0 ? atoi(state->settings_steam_cache_ttl_hours) : state->cfg->steam_cache_ttl_hours;
    state->cfg->steam_id = arena_strdup(state->arena, state->settings_steam_id);
    state->cfg->review_default_length = arena_strdup(state->arena, SETTINGS_REVIEW_LENGTHS[state->settings_review_length_index]);
    state->cfg->review_default_tone = arena_strdup(state->arena, SETTINGS_REVIEW_TONES[state->settings_review_tone_index]);
    state->cfg->review_default_vocab = arena_strdup(state->arena, SETTINGS_REVIEW_VOCABS[state->settings_review_vocab_index]);

    /* Only touch the key's storage if this field was actually edited this
     * session -- otherwise an unrelated Ctrl+S (e.g. just changing Model)
     * would silently copy an env-sourced key into config.ini, which was
     * never asked for. */
    if (strcmp(state->settings_steam_api_key, state->cfg->steam_api_key) != 0)
    {
        state->cfg->steam_api_key = arena_strdup(state->arena, state->settings_steam_api_key);
        state->cfg->steam_api_key_from_env = false;
    }
}

static tui_action_t handle_settings_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (state->settings_editing)
    {
        text_field_t f;

        switch (state->settings_field)
        {
            case TUI_SETTINGS_FIELD_MODEL:
                f = (text_field_t){state->settings_model, sizeof(state->settings_model), &state->settings_model_len, &state->settings_model_cursor};
                break;
            case TUI_SETTINGS_FIELD_BASE_URL:
                f = (text_field_t){state->settings_base_url, sizeof(state->settings_base_url), &state->settings_base_url_len, &state->settings_base_url_cursor};
                break;
            case TUI_SETTINGS_FIELD_NUM_CTX:
                f = (text_field_t){state->settings_num_ctx, sizeof(state->settings_num_ctx), &state->settings_num_ctx_len, &state->settings_num_ctx_cursor};
                break;
            case TUI_SETTINGS_FIELD_REVIEW_COUNT:
                f = (text_field_t){state->settings_review_count, sizeof(state->settings_review_count), &state->settings_review_count_len, &state->settings_review_count_cursor};
                break;
            case TUI_SETTINGS_FIELD_STEAM_LANGUAGE:
                f = (text_field_t){state->settings_steam_language, sizeof(state->settings_steam_language), &state->settings_steam_language_len, &state->settings_steam_language_cursor};
                break;
            case TUI_SETTINGS_FIELD_STEAM_CACHE_TTL_HOURS:
                f = (text_field_t){state->settings_steam_cache_ttl_hours, sizeof(state->settings_steam_cache_ttl_hours), &state->settings_steam_cache_ttl_hours_len, &state->settings_steam_cache_ttl_hours_cursor};
                break;
            case TUI_SETTINGS_FIELD_STEAM_ID:
                f = (text_field_t){state->settings_steam_id, sizeof(state->settings_steam_id), &state->settings_steam_id_len, &state->settings_steam_id_cursor};
                break;
            case TUI_SETTINGS_FIELD_STEAM_API_KEY:
                f = (text_field_t){state->settings_steam_api_key, sizeof(state->settings_steam_api_key), &state->settings_steam_api_key_len, &state->settings_steam_api_key_cursor};
                break;
            default:
                state->settings_editing = false;
                return TUI_ACTION_NONE;
        }

        if (key == TUI_KEY_ENTER)
        {
            state->settings_editing = false;
            return TUI_ACTION_NONE;
        }

        if (key == TUI_KEY_ESC)
        {
            settings_reset_field(state, state->settings_field);
            state->settings_editing = false;
            return TUI_ACTION_NONE;
        }

        bool numeric_only = (state->settings_field == TUI_SETTINGS_FIELD_NUM_CTX || state->settings_field == TUI_SETTINGS_FIELD_REVIEW_COUNT ||
                              state->settings_field == TUI_SETTINGS_FIELD_STEAM_CACHE_TTL_HOURS || state->settings_field == TUI_SETTINGS_FIELD_STEAM_ID);
        handle_text_field_key(&f, ch, key, numeric_only);
        return TUI_ACTION_NONE;
    }

    /* navigation mode: moving between fields, not editing one */
    if (ch == 'q')
    {
        state->screen = TUI_SCREEN_QUIT;
        return TUI_ACTION_QUIT;
    }

    if (key == TUI_KEY_ESC)
    {
        state->screen = state->settings_return_screen;
        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_CTRL_S)
    {
        settings_commit_to_cfg(state);
        return TUI_ACTION_SAVE_SETTINGS;
    }

    if ((key == TUI_KEY_ARROW_UP || ch == 'k') && state->settings_field > 0)
    {
        state->settings_field--;
        return TUI_ACTION_NONE;
    }

    if ((key == TUI_KEY_ARROW_DOWN || ch == 'j') && state->settings_field + 1 < TUI_SETTINGS_FIELD_COUNT)
    {
        state->settings_field++;
        return TUI_ACTION_NONE;
    }

    /* Provider, Review length/tone/vocab are all fixed-choice fields
     * cycled with h/l or the arrows, rather than free-text edited. */
    int *cycle_index = NULL;
    size_t cycle_count = 0;
    switch (state->settings_field)
    {
        case TUI_SETTINGS_FIELD_PROVIDER:
            cycle_index = &state->settings_provider_index;
            cycle_count = SETTINGS_PROVIDER_COUNT;
            break;
        case TUI_SETTINGS_FIELD_REVIEW_DEFAULT_LENGTH:
            cycle_index = &state->settings_review_length_index;
            cycle_count = SETTINGS_REVIEW_LENGTH_COUNT;
            break;
        case TUI_SETTINGS_FIELD_REVIEW_DEFAULT_TONE:
            cycle_index = &state->settings_review_tone_index;
            cycle_count = SETTINGS_REVIEW_TONE_COUNT;
            break;
        case TUI_SETTINGS_FIELD_REVIEW_DEFAULT_VOCAB:
            cycle_index = &state->settings_review_vocab_index;
            cycle_count = SETTINGS_REVIEW_VOCAB_COUNT;
            break;
        default:
            break;
    }

    if (cycle_index)
    {
        if (key == TUI_KEY_ARROW_LEFT || ch == 'h')
        {
            *cycle_index = (*cycle_index + (int)cycle_count - 1) % (int)cycle_count;
        }
        else if (key == TUI_KEY_ARROW_RIGHT || ch == 'l' || key == TUI_KEY_ENTER)
        {
            *cycle_index = (*cycle_index + 1) % (int)cycle_count;
        }
        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_ENTER)
    {
        state->settings_editing = true;
        return TUI_ACTION_NONE;
    }

    return TUI_ACTION_NONE;
}

/* Clears everything from a finished (or in-progress) review run that would
 * otherwise leak into the next one -- notes, rating, spoiler details,
 * follow-up Q&A, the generated review text itself -- before heading back
 * to pick another game. Deliberately leaves length/tone/vocab/bbcode and
 * the already-fetched library list alone: those are reusable session
 * preferences/data, not per-review state. game/themes/liked_cursor/etc.
 * aren't reset here either -- tui_apply_game_loaded already overwrites all
 * of them once the next game is actually picked. */
static void reset_for_next_game(app_state_t *state)
{
    adding_item_reset(state);

    state->notes_input[0] = '\0';
    state->notes_input_len = 0;
    state->notes_input_cursor = 0;

    state->rating_input[0] = '\0';
    state->rating_input_len = 0;
    state->rating_input_cursor = 0;
    state->rating = 0;
    state->recommend = false;

    state->spoiler_input[0] = '\0';
    state->spoiler_input_len = 0;
    state->spoiler_input_cursor = 0;

    state->followup_questions = NULL;
    state->n_followup_questions = 0;
    state->followup_index = 0;
    for (size_t i = 0; i < FOLLOWUP_MAX_QUESTIONS; i++)
    {
        state->followup_answer[i][0] = '\0';
        state->followup_answer_len[i] = 0;
        state->followup_answer_cursor[i] = 0;
    }

    state->review_text = NULL;
    state->status[0] = '\0';
}

static tui_action_t handle_result_key(app_state_t *state, uint32_t ch)
{
    if (ch == 'q')
    {
        state->screen = TUI_SCREEN_QUIT;
        return TUI_ACTION_QUIT;
    }

    if (ch == 'r')
    {
        state->screen = TUI_SCREEN_LOADING_REVIEW;
        return TUI_ACTION_GENERATE_REVIEW;
    }

    if (ch == 'e')
    {
        return TUI_ACTION_EDIT_REVIEW;
    }

    if (ch == 'c')
    {
        return TUI_ACTION_COPY_REVIEW;
    }

    if (ch == 's')
    {
        return TUI_ACTION_SAVE_REVIEW;
    }

    if (ch == 'o')
    {
        /* Back through Liked/Disliked/Notes with everything you answered
         * last time still in place -- for fixing a wrong like/dislike or
         * just wanting a different length/tone/vocab, without starting
         * the whole checklist over from what the AI extracted. */
        adding_item_reset(state);
        state->screen = TUI_SCREEN_LIKED;
        return TUI_ACTION_NONE;
    }

    if (ch == 'n')
    {
        /* Pick another game -- back to the already-fetched library list
         * when there is one (no need to refetch), else fall back to
         * fetching it fresh, same as the global F3 hotkey. */
        reset_for_next_game(state);
        if (state->library.count > 0)
        {
            state->screen = TUI_SCREEN_LIBRARY;
            return TUI_ACTION_NONE;
        }
        state->screen = TUI_SCREEN_LOADING_LIBRARY;
        return TUI_ACTION_LOAD_LIBRARY;
    }

    return TUI_ACTION_NONE;
}

tui_action_t tui_handle_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (key == TUI_KEY_CTRL_C)
    {
        state->screen = TUI_SCREEN_QUIT;
        return TUI_ACTION_QUIT;
    }

    if (key == TUI_KEY_F2 && state->screen != TUI_SCREEN_SETTINGS)
    {
        settings_enter(state);
        return TUI_ACTION_NONE;
    }

    if (key == TUI_KEY_F3 && state->screen != TUI_SCREEN_LIBRARY && state->screen != TUI_SCREEN_LOADING_LIBRARY)
    {
        /* Browse-by-playtime from anywhere, like F2 jumps to Settings from
         * anywhere -- including mid-flow (Liked/Notes/...), so whatever
         * review was in progress for the current game gets reset same as
         * 'n' on the Result screen does, not left to leak into the next
         * one once a game actually gets picked off the library list. */
        reset_for_next_game(state);
        state->screen = TUI_SCREEN_LOADING_LIBRARY;
        return TUI_ACTION_LOAD_LIBRARY;
    }

    switch (state->screen)
    {
        case TUI_SCREEN_SEARCH:
            return handle_search_key(state, ch, key);

        case TUI_SCREEN_SELECT:
            return handle_select_key(state, ch, key);

        case TUI_SCREEN_LIBRARY:
            return handle_library_key(state, ch, key);

        case TUI_SCREEN_LIKED:
            return handle_checklist_key(state, ch, key, true, TUI_SCREEN_DISLIKED, TUI_SCREEN_SELECT);

        case TUI_SCREEN_DISLIKED:
            return handle_checklist_key(state, ch, key, false, TUI_SCREEN_NOTES, TUI_SCREEN_LIKED);

        case TUI_SCREEN_NOTES:
            return handle_notes_key(state, ch, key);

        case TUI_SCREEN_RESULT:
            return handle_result_key(state, ch);

        case TUI_SCREEN_SETTINGS:
            return handle_settings_key(state, ch, key);

        case TUI_SCREEN_LOADING_LIBRARY:
        case TUI_SCREEN_LOADING_GAME:
        case TUI_SCREEN_LOADING_FOLLOWUP:
        case TUI_SCREEN_LOADING_REVIEW:
        case TUI_SCREEN_QUIT:
        default:
            return TUI_ACTION_NONE;
    }
}

void tui_apply_search_results(app_state_t *state, steam_search_results_t results)
{
    state->search_results = results;
    state->select_cursor = 0;
    state->status[0] = '\0';

    if (results.count == 0)
    {
        snprintf(state->status, sizeof(state->status), "No matches for \"%s\"", state->search_input);
        state->screen = TUI_SCREEN_SEARCH;
        return;
    }

    if (results.count == 1)
    {
        state->appid = results.items[0].appid;
        state->screen = TUI_SCREEN_LOADING_GAME;
        return;
    }

    state->screen = TUI_SCREEN_SELECT;
}

void tui_apply_search_failed(app_state_t *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "Search failed: %s", message);
    state->screen = TUI_SCREEN_SEARCH;
}

void tui_apply_library_loaded(app_state_t *state, steam_library_t library)
{
    state->library = library;
    state->library_cursor = 0;
    state->status[0] = '\0';

    if (library.count == 0)
    {
        snprintf(state->status, sizeof(state->status), "Your Steam library is empty (or your profile/game details are private)");
        state->screen = TUI_SCREEN_SEARCH;
        return;
    }

    state->screen = TUI_SCREEN_LIBRARY;
}

void tui_apply_library_load_failed(app_state_t *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "Could not load your library: %s", message);
    state->screen = TUI_SCREEN_SEARCH;
}

void tui_apply_game_loaded(app_state_t *state, uint32_t appid, game_info_t game, theme_set_t themes)
{
    state->appid = appid;
    state->game = game;
    state->themes = themes;
    state->liked_cursor = 0;
    state->disliked_cursor = 0;
    adding_item_reset(state);
    state->followup_questions = NULL;
    state->n_followup_questions = 0;
    state->followup_index = 0;
    state->status[0] = '\0';
    state->screen = TUI_SCREEN_LIKED;
}

void tui_apply_game_load_failed(app_state_t *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "Could not load game: %s", message);
    state->screen = TUI_SCREEN_SEARCH;
}

void tui_apply_followup_generated(app_state_t *state, char **questions, size_t count)
{
    if (count > FOLLOWUP_MAX_QUESTIONS) count = FOLLOWUP_MAX_QUESTIONS;

    state->followup_questions = questions;
    state->n_followup_questions = count;
    state->followup_index = 0;

    for (size_t i = 0; i < FOLLOWUP_MAX_QUESTIONS; i++)
    {
        state->followup_answer[i][0] = '\0';
        state->followup_answer_len[i] = 0;
        state->followup_answer_cursor[i] = 0;
    }

    state->notes_step = (count > 0) ? TUI_NOTES_STEP_FOLLOWUP : TUI_NOTES_STEP_TEXT;
    state->screen = TUI_SCREEN_NOTES;
}

void tui_apply_followup_failed(app_state_t *state)
{
    /* Best-effort, optional feature: on failure, skip straight to the
     * free-text notes step instead of surfacing an error for something
     * that was never required to begin with. */
    state->followup_questions = NULL;
    state->n_followup_questions = 0;
    state->followup_index = 0;
    state->notes_step = TUI_NOTES_STEP_TEXT;
    state->screen = TUI_SCREEN_NOTES;
}

void tui_apply_spoiler_predicted(app_state_t *state, const char *guess)
{
    state->notes_step = TUI_NOTES_STEP_SPOILER;
    state->screen = TUI_SCREEN_NOTES;

    if (guess && guess[0]) copy_into(state->spoiler_input, sizeof(state->spoiler_input), &state->spoiler_input_len, &state->spoiler_input_cursor, guess);
}

void tui_apply_spoiler_predict_failed(app_state_t *state)
{
    /* Best-effort, optional: on failure, the step just starts blank, same
     * as if nothing had matched. */
    state->notes_step = TUI_NOTES_STEP_SPOILER;
    state->screen = TUI_SCREEN_NOTES;
}

void tui_apply_review_generated(app_state_t *state, char *review_text)
{
    state->review_text = review_text;
    state->status[0] = '\0';
    state->screen = TUI_SCREEN_RESULT;
}

void tui_apply_review_failed(app_state_t *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "Could not generate review: %s", message);
    state->screen = TUI_SCREEN_RESULT;
}

void tui_apply_settings_saved(app_state_t *state)
{
    state->status[0] = '\0';
    state->screen = state->settings_return_screen;
}

void tui_apply_settings_save_failed(app_state_t *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "Could not save settings: %s", message);
}

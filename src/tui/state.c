#include "tui/state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tui/keys.h"

static const char *SETTINGS_PROVIDERS[] = {"anthropic", "openai", "ollama"};
#define SETTINGS_PROVIDER_COUNT (sizeof(SETTINGS_PROVIDERS) / sizeof(SETTINGS_PROVIDERS[0]))

static review_tone_t tone_from_config(const config_t *cfg)
{
    if (strcmp(cfg->review_default_tone, "neutral") == 0) return REVIEW_TONE_NEUTRAL;
    if (strcmp(cfg->review_default_tone, "enthusiastic") == 0) return REVIEW_TONE_ENTHUSIASTIC;
    return REVIEW_TONE_CASUAL;
}

void tui_state_init(app_state_t *state, arena_t *arena, config_t *cfg, const char *initial_query, uint32_t initial_appid)
{
    memset(state, 0, sizeof(*state));
    state->arena = arena;
    state->cfg = cfg;
    state->screen = TUI_SCREEN_SEARCH;
    state->tone = tone_from_config(cfg);
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

static tui_action_t handle_search_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (key == TUI_KEY_ENTER)
    {
        if (state->search_input_len == 0) return TUI_ACTION_NONE;
        return TUI_ACTION_SEARCH;
    }

    if (key == TUI_KEY_BACKSPACE || key == TUI_KEY_BACKSPACE2)
    {
        if (state->search_input_len > 0) state->search_input[--state->search_input_len] = '\0';
        return TUI_ACTION_NONE;
    }

    if (is_printable(ch) && state->search_input_len + 1 < sizeof(state->search_input))
    {
        state->search_input[state->search_input_len++] = (char)ch;
        state->search_input[state->search_input_len] = '\0';
    }

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

static tui_action_t handle_checklist_key(app_state_t *state, uint32_t ch, uint16_t key, theme_t *items, size_t count, size_t *cursor, tui_screen_t next_screen, tui_screen_t back_screen)
{
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

    if (key == TUI_KEY_ENTER)
    {
        state->screen = next_screen;
        return TUI_ACTION_NONE;
    }

    return TUI_ACTION_NONE;
}

static tui_action_t handle_notes_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    switch (state->notes_step)
    {
        case TUI_NOTES_STEP_TEXT:
            if (key == TUI_KEY_ESC)
            {
                state->screen = TUI_SCREEN_DISLIKED;
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_ENTER)
            {
                state->notes_step = TUI_NOTES_STEP_RECOMMEND;
                return TUI_ACTION_NONE;
            }
            if (key == TUI_KEY_BACKSPACE || key == TUI_KEY_BACKSPACE2)
            {
                if (state->notes_input_len > 0) state->notes_input[--state->notes_input_len] = '\0';
                return TUI_ACTION_NONE;
            }
            if (is_printable(ch) && state->notes_input_len + 1 < sizeof(state->notes_input))
            {
                state->notes_input[state->notes_input_len++] = (char)ch;
                state->notes_input[state->notes_input_len] = '\0';
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
                state->notes_step = TUI_NOTES_STEP_LENGTH;
            }
            else if (ch == 'n' || ch == 'N')
            {
                state->recommend = false;
                state->notes_step = TUI_NOTES_STEP_LENGTH;
            }
            return TUI_ACTION_NONE;

        case TUI_NOTES_STEP_LENGTH:
            if (key == TUI_KEY_ESC)
            {
                state->notes_step = TUI_NOTES_STEP_RECOMMEND;
                return TUI_ACTION_NONE;
            }
            if (ch == 's' || ch == 'S') state->length = REVIEW_LEN_SHORT;
            else if (ch == 'm' || ch == 'M') state->length = REVIEW_LEN_MEDIUM;
            else if (ch == 'l' || ch == 'L') state->length = REVIEW_LEN_LONG;
            else return TUI_ACTION_NONE;

            state->screen = TUI_SCREEN_LOADING_REVIEW;
            return TUI_ACTION_GENERATE_REVIEW;

        default:
            return TUI_ACTION_NONE;
    }
}

static void copy_into(char *dst, size_t dst_cap, size_t *dst_len, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_cap) len = dst_cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    *dst_len = len;
}

static void settings_enter(app_state_t *state)
{
    state->settings_return_screen = state->screen;
    state->screen = TUI_SCREEN_SETTINGS;
    state->settings_field = TUI_SETTINGS_FIELD_PROVIDER;
    state->settings_editing = false;
    state->status[0] = '\0';

    state->settings_provider_index = 0;
    for (size_t i = 0; i < SETTINGS_PROVIDER_COUNT; i++)
    {
        if (strcmp(state->cfg->llm_provider, SETTINGS_PROVIDERS[i]) == 0)
        {
            state->settings_provider_index = (int)i;
            break;
        }
    }

    copy_into(state->settings_model, sizeof(state->settings_model), &state->settings_model_len, state->cfg->llm_model);
    copy_into(state->settings_base_url, sizeof(state->settings_base_url), &state->settings_base_url_len, state->cfg->llm_base_url);

    char num_ctx_str[TUI_SETTINGS_NUM_CTX_CAP];
    snprintf(num_ctx_str, sizeof(num_ctx_str), "%d", state->cfg->llm_num_ctx);
    copy_into(state->settings_num_ctx, sizeof(state->settings_num_ctx), &state->settings_num_ctx_len, num_ctx_str);
}

/* Reverts one field's edit buffer back to *cfg's value (Esc-while-editing). */
static void settings_reset_field(app_state_t *state, tui_settings_field_t field)
{
    switch (field)
    {
        case TUI_SETTINGS_FIELD_MODEL:
            copy_into(state->settings_model, sizeof(state->settings_model), &state->settings_model_len, state->cfg->llm_model);
            break;
        case TUI_SETTINGS_FIELD_BASE_URL:
            copy_into(state->settings_base_url, sizeof(state->settings_base_url), &state->settings_base_url_len, state->cfg->llm_base_url);
            break;
        case TUI_SETTINGS_FIELD_NUM_CTX:
        {
            char num_ctx_str[TUI_SETTINGS_NUM_CTX_CAP];
            snprintf(num_ctx_str, sizeof(num_ctx_str), "%d", state->cfg->llm_num_ctx);
            copy_into(state->settings_num_ctx, sizeof(state->settings_num_ctx), &state->settings_num_ctx_len, num_ctx_str);
            break;
        }
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
}

static tui_action_t handle_settings_key(app_state_t *state, uint32_t ch, uint16_t key)
{
    if (state->settings_editing)
    {
        char *buf = NULL;
        size_t *len = NULL;
        size_t cap = 0;

        switch (state->settings_field)
        {
            case TUI_SETTINGS_FIELD_MODEL:
                buf = state->settings_model;
                len = &state->settings_model_len;
                cap = sizeof(state->settings_model);
                break;
            case TUI_SETTINGS_FIELD_BASE_URL:
                buf = state->settings_base_url;
                len = &state->settings_base_url_len;
                cap = sizeof(state->settings_base_url);
                break;
            case TUI_SETTINGS_FIELD_NUM_CTX:
                buf = state->settings_num_ctx;
                len = &state->settings_num_ctx_len;
                cap = sizeof(state->settings_num_ctx);
                break;
            default:
                break;
        }

        if (!buf)
        {
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

        if (key == TUI_KEY_BACKSPACE || key == TUI_KEY_BACKSPACE2)
        {
            if (*len > 0) buf[--(*len)] = '\0';
            return TUI_ACTION_NONE;
        }

        bool numeric_only = (state->settings_field == TUI_SETTINGS_FIELD_NUM_CTX);
        bool ok_char = numeric_only ? (ch >= '0' && ch <= '9') : is_printable(ch);

        if (ok_char && *len + 1 < cap)
        {
            buf[(*len)++] = (char)ch;
            buf[*len] = '\0';
        }

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

    if (state->settings_field == TUI_SETTINGS_FIELD_PROVIDER)
    {
        if (key == TUI_KEY_ARROW_LEFT || ch == 'h')
        {
            state->settings_provider_index = (state->settings_provider_index + (int)SETTINGS_PROVIDER_COUNT - 1) % (int)SETTINGS_PROVIDER_COUNT;
        }
        else if (key == TUI_KEY_ARROW_RIGHT || ch == 'l' || key == TUI_KEY_ENTER)
        {
            state->settings_provider_index = (state->settings_provider_index + 1) % (int)SETTINGS_PROVIDER_COUNT;
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

    switch (state->screen)
    {
        case TUI_SCREEN_SEARCH:
            return handle_search_key(state, ch, key);

        case TUI_SCREEN_SELECT:
            return handle_select_key(state, ch, key);

        case TUI_SCREEN_LIKED:
            return handle_checklist_key(state, ch, key, state->themes.liked, state->themes.n_liked, &state->liked_cursor, TUI_SCREEN_DISLIKED, TUI_SCREEN_SELECT);

        case TUI_SCREEN_DISLIKED:
            return handle_checklist_key(state, ch, key, state->themes.disliked, state->themes.n_disliked, &state->disliked_cursor, TUI_SCREEN_NOTES, TUI_SCREEN_LIKED);

        case TUI_SCREEN_NOTES:
            return handle_notes_key(state, ch, key);

        case TUI_SCREEN_RESULT:
            return handle_result_key(state, ch);

        case TUI_SCREEN_SETTINGS:
            return handle_settings_key(state, ch, key);

        case TUI_SCREEN_LOADING_GAME:
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

void tui_apply_game_loaded(app_state_t *state, uint32_t appid, game_info_t game, theme_set_t themes)
{
    state->appid = appid;
    state->game = game;
    state->themes = themes;
    state->liked_cursor = 0;
    state->disliked_cursor = 0;
    state->status[0] = '\0';
    state->screen = TUI_SCREEN_LIKED;
}

void tui_apply_game_load_failed(app_state_t *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "Could not load game: %s", message);
    state->screen = TUI_SCREEN_SEARCH;
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

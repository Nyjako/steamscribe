#include "tui/state.h"

#include <stdio.h>
#include <string.h>

#include "tui/keys.h"

void tui_state_init(app_state_t *state, arena_t *arena, review_tone_t default_tone, const char *initial_query, uint32_t initial_appid)
{
    memset(state, 0, sizeof(*state));
    state->arena = arena;
    state->screen = TUI_SCREEN_SEARCH;
    state->tone = default_tone;
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

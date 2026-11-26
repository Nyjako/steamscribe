#ifndef STEAMSCRIBE_TUI_STATE_H
#define STEAMSCRIBE_TUI_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "core/followup.h"
#include "core/review.h"
#include "core/themes.h"
#include "steam/steam.h"
#include "util/arena.h"

#define TUI_SEARCH_INPUT_CAP 128
#define TUI_NOTES_INPUT_CAP 8192 /* generous -- a multi-paragraph note shouldn't hit this */
#define TUI_STATUS_CAP 512 /* matches err_t.msg so a wrapped err.msg never gets truncated again here */
#define TUI_SETTINGS_MODEL_CAP 128
#define TUI_SETTINGS_URL_CAP 256
#define TUI_SETTINGS_NUM_CTX_CAP 16
#define TUI_SETTINGS_REVIEW_COUNT_CAP 16
#define TUI_CUSTOM_ITEM_CAP 256

typedef enum
{
    TUI_SCREEN_SEARCH,
    TUI_SCREEN_SELECT,
    TUI_SCREEN_LOADING_GAME,
    TUI_SCREEN_LIKED,
    TUI_SCREEN_DISLIKED,
    TUI_SCREEN_LOADING_FOLLOWUP,
    TUI_SCREEN_NOTES,
    TUI_SCREEN_LOADING_REVIEW,
    TUI_SCREEN_RESULT,
    TUI_SCREEN_SETTINGS,
    TUI_SCREEN_QUIT,
} tui_screen_t;

typedef enum
{
    TUI_NOTES_STEP_FOLLOWUP,
    TUI_NOTES_STEP_TEXT,
    TUI_NOTES_STEP_RECOMMEND,
    TUI_NOTES_STEP_RATING,
    TUI_NOTES_STEP_LENGTH,
    TUI_NOTES_STEP_VOCAB,
    TUI_NOTES_STEP_FORMAT,
    TUI_NOTES_STEP_SPOILER,
} tui_notes_step_t;

typedef enum
{
    TUI_SETTINGS_FIELD_PROVIDER,
    TUI_SETTINGS_FIELD_MODEL,
    TUI_SETTINGS_FIELD_BASE_URL,
    TUI_SETTINGS_FIELD_NUM_CTX,
    TUI_SETTINGS_FIELD_REVIEW_COUNT,
    TUI_SETTINGS_FIELD_COUNT,
} tui_settings_field_t;

typedef enum
{
    TUI_ACTION_NONE,
    TUI_ACTION_QUIT,
    TUI_ACTION_SEARCH,          /* run a search for state->search_input */
    TUI_ACTION_LOAD_GAME,       /* fetch game info + themes for state->appid */
    TUI_ACTION_GENERATE_FOLLOWUP, /* generate optional follow-up questions from state->themes */
    TUI_ACTION_GENERATE_REVIEW, /* call the LLM with state's notes/recommend/length/tone */
    TUI_ACTION_SAVE_SETTINGS,   /* write state->cfg (already updated) to config.ini */
    TUI_ACTION_EDIT_REVIEW,     /* open state->review_text in $EDITOR, then update it */
    TUI_ACTION_COPY_REVIEW,     /* copy state->review_text to the clipboard */
} tui_action_t;

typedef struct app_state_t
{
    arena_t *arena;
    config_t *cfg; /* non-owning; live config, updated (and optionally saved) from Settings */

    tui_screen_t screen;
    char status[TUI_STATUS_CAP];

    char search_input[TUI_SEARCH_INPUT_CAP];
    size_t search_input_len;
    size_t search_input_cursor;

    steam_search_results_t search_results;
    size_t select_cursor;

    uint32_t appid;
    game_info_t game;
    theme_set_t themes;

    size_t liked_cursor;
    size_t disliked_cursor;

    /* Liked/Disliked screens: typing a custom item to add to the checklist,
     * beyond what themes_extract surfaced. */
    bool adding_item;
    char custom_item_input[TUI_CUSTOM_ITEM_CAP];
    size_t custom_item_input_len;
    size_t custom_item_input_cursor;

    /* Optional, AI-generated follow-up questions asked right after
     * Disliked -- best-effort: generation failure just means zero
     * questions, never a hard error that blocks the flow. */
    char **followup_questions;
    size_t n_followup_questions;
    size_t followup_index;
    char followup_answer[FOLLOWUP_MAX_QUESTIONS][TUI_NOTES_INPUT_CAP];
    size_t followup_answer_len[FOLLOWUP_MAX_QUESTIONS];
    size_t followup_answer_cursor[FOLLOWUP_MAX_QUESTIONS];

    char notes_input[TUI_NOTES_INPUT_CAP];
    size_t notes_input_len;
    size_t notes_input_cursor;
    tui_notes_step_t notes_step;
    bool recommend;
    char rating_input[3]; /* "0".."10", blank means no rating given */
    size_t rating_input_len;
    size_t rating_input_cursor;
    int rating; /* 0 = none, else 1-10; parsed from rating_input on continue */
    review_len_t length;
    review_tone_t tone;
    review_vocab_t vocab;
    bool bbcode; /* false = plain, true = bbcode (affects the spoiler block below) */

    char spoiler_input[TUI_NOTES_INPUT_CAP];
    size_t spoiler_input_len;
    size_t spoiler_input_cursor;

    char *review_text;

    /* Settings: a staging copy of the relevant config_t fields, edited here
     * and only written back to *cfg on save (Ctrl+S); Esc discards them. */
    tui_screen_t settings_return_screen;
    tui_settings_field_t settings_field;
    bool settings_editing;
    int settings_provider_index; /* into the fixed anthropic/openai/ollama list */
    char settings_model[TUI_SETTINGS_MODEL_CAP];
    size_t settings_model_len;
    size_t settings_model_cursor;
    char settings_base_url[TUI_SETTINGS_URL_CAP];
    size_t settings_base_url_len;
    size_t settings_base_url_cursor;
    char settings_num_ctx[TUI_SETTINGS_NUM_CTX_CAP];
    size_t settings_num_ctx_len;
    size_t settings_num_ctx_cursor;
    char settings_review_count[TUI_SETTINGS_REVIEW_COUNT_CAP];
    size_t settings_review_count_len;
    size_t settings_review_count_cursor;
} app_state_t;

/* Zeroes `state` and sets it up to start either on the Search screen
 * (pre-filled with `initial_query` if given), or -- when `initial_appid` is
 * nonzero -- straight on the Loading-game screen, skipping Search/Select
 * entirely (the `--appid` path). `cfg` is kept (not copied) and may be
 * updated by the Settings screen. */
void tui_state_init(app_state_t *state, arena_t *arena, config_t *cfg, const char *initial_query, uint32_t initial_appid);

/* Pure key handling: mutates only UI-local state (cursors, buffers, which
 * screen is current) and returns which IO action, if any, the caller must
 * now perform. Never touches the network, the LLM, or termbox2 itself --
 * safe to call with made-up `ch`/`key` values in a test. */
tui_action_t tui_handle_key(app_state_t *state, uint32_t ch, uint16_t key);

/* Pure result-folding: the caller performs the IO action tui_handle_key
 * asked for, then calls the matching one of these to apply the outcome and
 * move to the next screen. */
void tui_apply_search_results(app_state_t *state, steam_search_results_t results);
void tui_apply_search_failed(app_state_t *state, const char *message);
void tui_apply_game_loaded(app_state_t *state, uint32_t appid, game_info_t game, theme_set_t themes);
void tui_apply_game_load_failed(app_state_t *state, const char *message);
void tui_apply_followup_generated(app_state_t *state, char **questions, size_t count);
void tui_apply_followup_failed(app_state_t *state);
void tui_apply_review_generated(app_state_t *state, char *review_text);
void tui_apply_review_failed(app_state_t *state, const char *message);
void tui_apply_settings_saved(app_state_t *state);
void tui_apply_settings_save_failed(app_state_t *state, const char *message);

#endif

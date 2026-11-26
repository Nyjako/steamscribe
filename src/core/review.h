#ifndef STEAMSCRIBE_CORE_REVIEW_H
#define STEAMSCRIBE_CORE_REVIEW_H

#include <stdbool.h>

#include "core/themes.h"
#include "llm/llm.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

typedef enum
{
    REVIEW_LEN_SHORT,
    REVIEW_LEN_MEDIUM,
    REVIEW_LEN_LONG,
} review_len_t;

typedef enum
{
    REVIEW_TONE_CASUAL,
    REVIEW_TONE_NEUTRAL,
    REVIEW_TONE_ENTHUSIASTIC,
} review_tone_t;

typedef enum
{
    REVIEW_VOCAB_NORMAL,
    REVIEW_VOCAB_SIMPLE, /* plain, everyday words -- no fancy vocabulary */
} review_vocab_t;

typedef struct review_request_t
{
    const game_info_t *game;
    const theme_set_t *themes; /* only entries with checked == true are used */
    const char *notes;         /* free text, may be empty */
    bool recommend;
    int rating;                /* 0 = none given; else 1-10 */
    review_len_t length;
    review_tone_t tone;
    review_vocab_t vocab;

    /* 0 = unknown/not included; else the player's own playtime for this
     * game, from the Steam Web API (best-effort, see pipeline_fetch_playtime). */
    int playtime_hours;

    /* Optional, AI-generated follow-up questions and the player's answers
     * (parallel arrays of n_followup entries). A blank/empty answer means
     * that question was skipped and is left out of the prompt. May be NULL
     * with n_followup == 0 if none were generated or asked. */
    char **followup_questions;
    char **followup_answers;
    size_t n_followup;

    bool bbcode; /* wrap spoiler_notes in [spoiler][/spoiler] instead of a plain label */

    /* Spoiler details, typed in their own step right before generating and
     * never passed to the model -- appended to the generated text exactly
     * as written, so the model can't paraphrase or drop them. May be empty. */
    const char *spoiler_notes;
} review_request_t;

/* Builds the review-generation system/user prompt for `req`, using only the
 * checked themes, the user's own notes, and their recommendation. Both
 * output strings are arena-allocated. Pure (no network), safe to test. */
void review_build_prompt(arena_t *arena, const review_request_t *req, char **out_system, char **out_user);

/* Appends req->spoiler_notes to generated_text verbatim, if non-empty --
 * wrapped in [spoiler][/spoiler] when req->bbcode, otherwise under a plain
 * "Spoilers:" label. A no-op (returns generated_text unchanged) when
 * spoiler_notes is empty. Pure, arena-allocated, safe to test. */
char *review_append_spoiler(arena_t *arena, const review_request_t *req, const char *generated_text);

/* Builds the prompt and calls provider->complete() to generate the review text. */
int review_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const review_request_t *req, char **out_text, err_t *err);

#endif

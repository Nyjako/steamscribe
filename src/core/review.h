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

typedef struct review_request_t
{
    const game_info_t *game;
    const theme_set_t *themes; /* only entries with checked == true are used */
    const char *notes;         /* free text, may be empty */
    bool recommend;
    review_len_t length;
    review_tone_t tone;
} review_request_t;

/* Builds the review-generation system/user prompt for `req`, using only the
 * checked themes, the user's own notes, and their recommendation. Both
 * output strings are arena-allocated. Pure (no network), safe to test. */
void review_build_prompt(arena_t *arena, const review_request_t *req, char **out_system, char **out_user);

/* Builds the prompt and calls provider->complete() to generate the review text. */
int review_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const review_request_t *req, char **out_text, err_t *err);

#endif

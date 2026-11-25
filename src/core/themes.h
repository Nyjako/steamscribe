#ifndef STEAMSCRIBE_CORE_THEMES_H
#define STEAMSCRIBE_CORE_THEMES_H

#include <stdbool.h>
#include <stddef.h>

#include "llm/llm.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

typedef struct theme_t
{
    char *id;
    char *label;
    bool checked;
} theme_t;

typedef struct theme_set_t
{
    theme_t *liked;
    size_t n_liked;
    theme_t *disliked;
    size_t n_disliked;
} theme_set_t;

/* Builds the theme-extraction system/user prompt for `game`/`reviews`. Both
 * output strings are arena-allocated. Pure (no network), safe to test. */
void themes_build_prompt(arena_t *arena, const game_info_t *game, const review_set_t *reviews, char **out_system, char **out_user);

/* Parses the LLM's response (optionally wrapped in a ```json code fence)
 * into `out`. Pure (no network), safe to test against canned text. */
int themes_parse_response(arena_t *arena, const char *llm_text, theme_set_t *out, err_t *err);

/* Builds the prompt, calls provider->complete(), and parses the result,
 * retrying once with a stricter instruction if the first parse fails. */
int themes_extract(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const review_set_t *reviews, theme_set_t *out, err_t *err);

#endif

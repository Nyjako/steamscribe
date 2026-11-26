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

/* Builds the system/user prompt for extracting just one side (liked when
 * is_liked_side, disliked otherwise) from only that side's reviews, capped
 * to a sane count per call -- good for small-context local models. When
 * `num_ctx` is set (ollama only; pass 0/cfg->num_ctx for other providers),
 * reviews are also trimmed by total length so the prompt can't overflow
 * that context window regardless of how verbose this particular game's
 * reviews happen to be -- a game with long, BBCode-heavy reviews could
 * otherwise build a prompt the model rejects outright. Both output strings
 * are arena-allocated. Pure (no network), safe to test. */
void themes_build_side_prompt(arena_t *arena, const game_info_t *game, const review_set_t *reviews, bool is_liked_side, int num_ctx, char **out_system, char **out_user);

/* Parses the LLM's single-side response (shape {"liked":[...]} or
 * {"disliked":[...]}, optionally wrapped in a ```json code fence) into
 * out_items/out_count. Pure (no network), safe to test against canned text. */
int themes_parse_side_response(arena_t *arena, const char *llm_text, bool is_liked_side, theme_t **out_items, size_t *out_count, err_t *err);

/* Parses the combined {"liked":[...],"disliked":[...]} shape themes_to_json
 * writes, for reading it back from the cache. */
int themes_parse_response(arena_t *arena, const char *llm_text, theme_set_t *out, err_t *err);

/* Extracts themes in two passes -- liked from only the positive reviews,
 * disliked from only the negative ones -- and merges the results. When
 * `provider` can enforce a JSON schema (its complete_json is non-NULL),
 * each pass uses that; otherwise it falls back to prompt-based JSON
 * instructions with one retry on invalid JSON, same as before. */
int themes_extract(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const review_set_t *reviews, theme_set_t *out, err_t *err);

/* Serializes `themes` back to the same {"liked":[...],"disliked":[...]} shape
 * themes_parse_response reads, for printing and for caching to themes.json.
 * Returns a malloc'd string (the caller frees it), or NULL on OOM. */
char *themes_to_json(const theme_set_t *themes);

/* Appends a user-typed item (checked by default, since the user is adding it
 * specifically to include it) to items/count, growing the array from
 * `arena`. For checklist entries the LLM didn't surface. Not persisted to
 * the themes cache -- callers add these to the in-memory set used for one
 * review, not to what themes_extract returns. */
void themes_add_custom_item(arena_t *arena, theme_t **items, size_t *count, const char *label);

/* Moves the item at `index` from the from side to the end of the to side,
 * preserving its id/label/checked state -- for when
 * the AI filed something under the wrong side of a user's actual opinion.
 * A no-op if `index` is out of range. */
void themes_move_item(arena_t *arena, theme_t **from_items, size_t *from_count, theme_t **to_items, size_t *to_count, size_t index);

#endif

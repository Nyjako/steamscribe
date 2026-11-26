#ifndef STEAMSCRIBE_CORE_FOLLOWUP_H
#define STEAMSCRIBE_CORE_FOLLOWUP_H

#include <stddef.h>

#include "core/themes.h"
#include "llm/llm.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

/* Caps how many follow-up questions a single generation call can return --
 * keeps the prompt/response small and bounds a caller's per-question
 * answer buffers (e.g. the TUI's fixed-size array). */
#define FOLLOWUP_MAX_QUESTIONS 3

/* Builds the system/user prompt for generating a handful of optional,
 * game-specific follow-up questions from the player's *checked* liked/
 * disliked items -- meant to surface concrete, personal detail that a
 * plain "anything else?" free-text field tends to miss. Pure (no
 * network), safe to test. */
void followup_build_prompt(arena_t *arena, const game_info_t *game, const theme_set_t *themes, char **out_system, char **out_user);

/* Parses the LLM's {"questions": ["...", ...]} response (optionally
 * ```json-fenced), clamped to FOLLOWUP_MAX_QUESTIONS. Pure, safe to test
 * against canned text. */
int followup_parse_response(arena_t *arena, const char *llm_text, char ***out_questions, size_t *out_count, err_t *err);

/* Generates the questions: builds the prompt, calls the provider (using
 * complete_json when available), and retries once on invalid JSON -- same
 * pattern as themes_extract. */
int followup_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const theme_set_t *themes, char ***out_questions, size_t *out_count, err_t *err);

#endif

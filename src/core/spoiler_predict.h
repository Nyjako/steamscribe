#ifndef STEAMSCRIBE_CORE_SPOILER_PREDICT_H
#define STEAMSCRIBE_CORE_SPOILER_PREDICT_H

#include <stddef.h>

#include "llm/llm.h"
#include "util/arena.h"
#include "util/error.h"

/* Builds the system/user prompt for predicting which part, if any, of the
 * player's own notes/follow-up answers about `game_name` sounds like a
 * plot spoiler (an ending, a major death, a twist, a reveal) -- meant as a
 * best-guess pre-fill for the Spoiler step, never as the final word on
 * what's actually a spoiler. `answers` holds `n_answers` follow-up answers,
 * any of which may be NULL or empty. Pure (no network), safe to test. */
void spoiler_predict_build_prompt(arena_t *arena, const char *game_name, const char *notes, const char *const *answers, size_t n_answers, char **out_system, char **out_user);

/* Parses the LLM's {"spoiler": "..."} response (optionally ```json-fenced).
 * An empty string is a valid, common answer (nothing looked spoiler-y).
 * Pure, safe to test against canned text. */
int spoiler_predict_parse_response(arena_t *arena, const char *llm_text, char **out_spoiler, err_t *err);

/* Generates the guess: builds the prompt, calls the provider (using
 * complete_json when available), and retries once on invalid JSON -- same
 * best-effort pattern as followup_generate. A failure here should never be
 * treated as a hard error by the caller -- just fall back to an empty
 * guess, the same as if nothing had matched. */
int spoiler_predict_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const char *game_name, const char *notes, const char *const *answers, size_t n_answers, char **out_spoiler, err_t *err);

#endif

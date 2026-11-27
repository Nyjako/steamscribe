#ifndef STEAMSCRIBE_CORE_PIPELINE_H
#define STEAMSCRIBE_CORE_PIPELINE_H

#include <stdint.h>

#include "config.h"
#include "core/themes.h"
#include "llm/llm.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

/* The shared fetch-or-cache/call-the-LLM pipeline, used by both the plain
 * CLI (main.c) and the TUI (tui/app.c) so neither duplicates it. */

/* Resolves which llm_provider/llm_cfg_t a config_t names. */
int pipeline_resolve_llm(const config_t *cfg, const llm_provider_t **out_provider, llm_cfg_t *out_cfg, err_t *err);

/* Fetches appdetails for `appid`, serving a fresh cache entry when there is
 * one and caching a fresh fetch otherwise. */
int pipeline_fetch_game(arena_t *arena, const config_t *cfg, uint32_t appid, game_info_t *out, err_t *err);

/* Fetches extracted liked/disliked themes for `appid`, serving a fresh cache
 * entry when there is one and sampling reviews + calling the LLM otherwise. */
int pipeline_fetch_themes(arena_t *arena, const config_t *cfg, uint32_t appid, const game_info_t *game, theme_set_t *out, err_t *err);

/* Best-effort: the player's own playtime for `appid`, if `cfg->steam_api_key`
 * and `cfg->steam_id` are both set and the Steam Web API call succeeds.
 * Never fails -- a missing key/id or a failed request just leaves
 * `out->found` false (logged via debug_logf, not surfaced as an error),
 * same "optional, silently skipped" pattern as the follow-up questions. */
void pipeline_fetch_playtime(const config_t *cfg, uint32_t appid, steam_playtime_t *out);

/* The player's full owned-games library, sorted most-played first, for
 * browsing by playtime instead of typing a game name to search for. Unlike
 * pipeline_fetch_playtime this is NOT best-effort: a missing
 * steam_api_key/steam_id is reported as an error (there's nothing useful to
 * show instead of a library), same "fail fast with a precise message"
 * pattern as pipeline_resolve_llm. */
int pipeline_fetch_library(arena_t *arena, const config_t *cfg, steam_library_t *out, err_t *err);

/* Best-effort: asks `provider` to free whatever it's holding onto for
 * `llm_cfg->model` (e.g. unload it from VRAM) now that a review has been
 * generated, instead of leaving it reserved until its own idle timeout.
 * A no-op for a provider with nothing to free (`provider->unload == NULL`,
 * true for every hosted API). Never fails outwardly -- a failed unload is
 * just logged via debug_logf, same "optional, silently skipped" pattern as
 * pipeline_fetch_playtime. Call after the review itself is generated, not
 * after every intermediate LLM call (themes, follow-up, ...), so those
 * still benefit from the model staying loaded between calls. */
void pipeline_unload_llm_model(const llm_provider_t *provider, const llm_cfg_t *llm_cfg);

/* Resolves `provider_name`/`model`/`base_url`/`num_ctx` exactly like
 * pipeline_resolve_llm (same fail-fast missing-key checks), then makes one
 * real, minimal completion call to actually confirm the provider/model/
 * endpoint combination works end to end -- not just that a key is present.
 * Meant for the TUI Settings screen's "test connection" action: the four
 * values are taken as given (typically the screen's staged, not-yet-saved
 * edits), not read from `cfg`, which only supplies the parts Settings
 * doesn't stage itself (the env-sourced Anthropic/OpenAI keys). On success,
 * *out_reply is the model's (arena-allocated) raw reply text -- not
 * validated beyond "the call itself succeeded", since providers are free
 * to answer a trivial prompt however they like. */
int pipeline_test_llm_connection(arena_t *arena, const config_t *cfg, const char *provider_name, const char *model, const char *base_url, int num_ctx, char **out_reply, err_t *err);

#endif

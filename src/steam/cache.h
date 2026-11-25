#ifndef STEAMSCRIBE_STEAM_CACHE_H
#define STEAMSCRIBE_STEAM_CACHE_H

#include <stdint.h>

#include "util/arena.h"
#include "util/error.h"

#define CACHE_TTL_HOURS_DEFAULT 24

/* Looks up the cache entry for (appid, name) (e.g. name = "details" or
 * "reviews"). Returns 1 on a fresh hit (*out_json copied from arena), 0 on
 * a miss or a stale entry, -1 on error. */
int cache_get(arena_t *arena, uint32_t appid, const char *name, int ttl_hours, char **out_json, err_t *err);

/* Writes json to the cache entry for (appid, name), creating directories as needed. */
int cache_put(uint32_t appid, const char *name, const char *json, err_t *err);

#endif

#ifndef STEAMSCRIBE_UTIL_JSON_FENCE_H
#define STEAMSCRIBE_UTIL_JSON_FENCE_H

#include "util/arena.h"

/* Strips a leading/trailing ```[json] code fence if the LLM wrapped its
 * answer in one, leaving plain JSON text untouched. Shared by every module
 * that parses a raw LLM response as JSON. */
char *json_strip_code_fences(arena_t *arena, const char *text);

#endif

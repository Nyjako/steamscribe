#ifndef STEAMSCRIBE_UTIL_TEXT_H
#define STEAMSCRIBE_UTIL_TEXT_H

#include <stddef.h>

#include "util/arena.h"

/* Word-wraps `text` to `width` columns (width >= 1), returning an
 * arena-allocated array of arena-allocated, NUL-terminated lines.
 * *out_count receives the number of lines. Existing '\n' characters in
 * `text` are hard breaks (so an empty line in the input stays a blank
 * line in the output); a single word longer than `width` (e.g. a long URL)
 * is hard-broken into width-sized chunks rather than ever producing a line
 * wider than `width`. */
char **text_wrap(arena_t *arena, const char *text, int width, size_t *out_count);

#endif

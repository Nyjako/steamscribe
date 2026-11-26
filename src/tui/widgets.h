#ifndef STEAMSCRIBE_TUI_WIDGETS_H
#define STEAMSCRIBE_TUI_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>

#include "core/themes.h"
#include "steam/steam.h"
#include "util/arena.h"

void widgets_draw_header(int y, const char *text);

/* Word-wraps `text` (e.g. an error message) across as many lines as it
 * needs, anchored to the bottom of the screen, instead of silently
 * truncating it to one row -- termbox2 clips text past the terminal width
 * with no visible sign there was more. */
void widgets_draw_status_bar(arena_t *arena, const char *text);
void widgets_draw_search_results(int y, const steam_search_results_t *results, size_t cursor);

/* Like widgets_draw_search_results, but for the (potentially much longer)
 * owned-games library: shows each entry's playtime, and scrolls a window
 * of rows to keep `cursor` on screen instead of overflowing the terminal
 * for a library with more games than fit. */
void widgets_draw_library(int y, const steam_library_t *library, size_t cursor);
void widgets_draw_checklist(int y, const theme_t *items, size_t count, size_t cursor);
void widgets_draw_wrapped_text(arena_t *arena, int x, int y, int width, const char *text);

/* Renders an editable, multi-line-capable text buffer starting at row y,
 * hard-wrapping (not word-wrapping) any line that would overflow `width`
 * characters. Hard-wrapping keeps an exact, reversible mapping between a
 * byte offset into `buffer` and its on-screen (row, col) -- required to
 * place the cursor anywhere in the middle of the text, not just at the
 * end, which word-wrapping (text_wrap) can't give you: it collapses or
 * reflows whitespace, so a byte offset doesn't correspond to a fixed
 * screen position. Splits on literal '\n' for multi-line buffers (e.g. the
 * notes field, where Enter inserts a newline). Places the real terminal
 * cursor at `cursor_pos` (a byte offset into `buffer`, 0..strlen(buffer)).
 * Returns the number of rows drawn (always at least 1), so the caller can
 * position whatever comes next. */
size_t widgets_draw_text_area(int y, int width, const char *buffer, size_t cursor_pos);

#endif

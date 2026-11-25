#ifndef STEAMSCRIBE_TUI_WIDGETS_H
#define STEAMSCRIBE_TUI_WIDGETS_H

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
void widgets_draw_checklist(int y, const theme_t *items, size_t count, size_t cursor);
void widgets_draw_text_input(int y, const char *label, const char *buffer);
void widgets_draw_wrapped_text(arena_t *arena, int x, int y, int width, const char *text);

#endif

#include "tui/widgets.h"

#include <string.h>

#include <termbox2.h>

#include "util/text.h"

void widgets_draw_header(int y, const char *text)
{
    tb_print(2, y, TB_DEFAULT | TB_BOLD, TB_DEFAULT, text);
}

#define STATUS_BAR_MAX_LINES 6

void widgets_draw_status_bar(arena_t *arena, const char *text)
{
    if (!text || !text[0]) return;

    int width = tb_width() - 4;
    if (width < 1) width = 1;

    size_t count = 0;
    char **lines = text_wrap(arena, text, width, &count);
    if (count > STATUS_BAR_MAX_LINES) count = STATUS_BAR_MAX_LINES;

    int start_y = tb_height() - (int)count;
    for (size_t i = 0; i < count; i++) tb_print(2, start_y + (int)i, TB_WHITE, TB_RED, lines[i]);
}

void widgets_draw_search_results(int y, const steam_search_results_t *results, size_t cursor)
{
    for (size_t i = 0; i < results->count; i++)
    {
        uintattr_t fg = (i == cursor) ? TB_BLACK : TB_DEFAULT;
        uintattr_t bg = (i == cursor) ? TB_CYAN : TB_DEFAULT;
        tb_printf(2, y + (int)i, fg, bg, "%s (%u)", results->items[i].name, results->items[i].appid);
    }
}

void widgets_draw_library(int y, const steam_library_t *library, size_t cursor)
{
    int available = tb_height() - y - 1;
    if (available < 1) available = 1;

    size_t start = 0;
    if (library->count > (size_t)available)
    {
        if (cursor >= (size_t)available) start = cursor - (size_t)available + 1;

        size_t max_start = library->count - (size_t)available;
        if (start > max_start) start = max_start;
    }

    size_t end = start + (size_t)available;
    if (end > library->count) end = library->count;

    for (size_t i = start; i < end; i++)
    {
        uintattr_t fg = (i == cursor) ? TB_BLACK : TB_DEFAULT;
        uintattr_t bg = (i == cursor) ? TB_CYAN : TB_DEFAULT;
        double hours = library->items[i].playtime_minutes / 60.0;
        tb_printf(2, y + (int)(i - start), fg, bg, "%7.1fh  %s", hours, library->items[i].name);
    }
}

void widgets_draw_checklist(int y, const theme_t *items, size_t count, size_t cursor)
{
    for (size_t i = 0; i < count; i++)
    {
        uintattr_t fg = (i == cursor) ? TB_BLACK : TB_DEFAULT;
        uintattr_t bg = (i == cursor) ? TB_CYAN : TB_DEFAULT;
        tb_printf(2, y + (int)i, fg, bg, "[%c] %s", items[i].checked ? 'x' : ' ', items[i].label);
    }
}

void widgets_draw_wrapped_text(arena_t *arena, int x, int y, int width, const char *text)
{
    size_t count = 0;
    char **lines = text_wrap(arena, text, width, &count);
    for (size_t i = 0; i < count; i++) tb_print(x, y + (int)i, TB_DEFAULT, TB_DEFAULT, lines[i]);
}

size_t widgets_draw_text_area(int y, int width, const char *buffer, size_t cursor_pos)
{
    if (width < 1) width = 1;

    int row = 0;
    int col = 0;
    int cursor_row = 0;
    int cursor_col = 0;
    bool cursor_found = false;

    size_t len = strlen(buffer);
    for (size_t i = 0; i < len; i++)
    {
        if (i == cursor_pos)
        {
            cursor_row = row;
            cursor_col = col;
            cursor_found = true;
        }

        if (buffer[i] == '\n')
        {
            row++;
            col = 0;
            continue;
        }

        tb_set_cell(2 + col, y + row, (uint32_t)(unsigned char)buffer[i], TB_DEFAULT, TB_DEFAULT);
        col++;

        if (col >= width)
        {
            row++;
            col = 0;
        }
    }

    if (!cursor_found)
    {
        cursor_row = row;
        cursor_col = col;
    }

    tb_set_cursor(2 + cursor_col, y + cursor_row);

    return (size_t)row + 1;
}

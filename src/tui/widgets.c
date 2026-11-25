#include "tui/widgets.h"

#include <string.h>

#include <termbox2.h>

#include "util/text.h"

void widgets_draw_header(int y, const char *text)
{
    tb_print(2, y, TB_DEFAULT | TB_BOLD, TB_DEFAULT, text);
}

void widgets_draw_status_bar(const char *text)
{
    if (!text || !text[0]) return;
    tb_print(2, tb_height() - 1, TB_WHITE, TB_RED, text);
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

void widgets_draw_checklist(int y, const theme_t *items, size_t count, size_t cursor)
{
    for (size_t i = 0; i < count; i++)
    {
        uintattr_t fg = (i == cursor) ? TB_BLACK : TB_DEFAULT;
        uintattr_t bg = (i == cursor) ? TB_CYAN : TB_DEFAULT;
        tb_printf(2, y + (int)i, fg, bg, "[%c] %s", items[i].checked ? 'x' : ' ', items[i].label);
    }
}

void widgets_draw_text_input(int y, const char *label, const char *buffer)
{
    tb_printf(2, y, TB_DEFAULT, TB_DEFAULT, "%s %s", label, buffer);
    tb_set_cursor(2 + (int)strlen(label) + 1 + (int)strlen(buffer), y);
}

void widgets_draw_wrapped_text(arena_t *arena, int x, int y, int width, const char *text)
{
    size_t count = 0;
    char **lines = text_wrap(arena, text, width, &count);
    for (size_t i = 0; i < count; i++) tb_print(x, y + (int)i, TB_DEFAULT, TB_DEFAULT, lines[i]);
}

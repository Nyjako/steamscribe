#include "util/text.h"

#include <goblin/dyn_array.h>

#include "util/strbuf.h"

GOBLIN_DYN_ARRAY_DECL(line_ptr, char *)

char **text_wrap(arena_t *arena, const char *text, int width, size_t *out_count)
{
    if (!text || !text[0])
    {
        *out_count = 0;
        return NULL;
    }

    line_ptr_array lines;
    line_ptr_array_init(&lines);

    strbuf_t current;
    strbuf_init(&current);

    const char *p = text;
    while (*p)
    {
        const char *para_start = p;
        while (*p && *p != '\n') p++;
        size_t para_len = (size_t)(p - para_start);

        size_t i = 0;
        while (i < para_len)
        {
            while (i < para_len && (para_start[i] == ' ' || para_start[i] == '\t')) i++;

            size_t word_start = i;
            while (i < para_len && para_start[i] != ' ' && para_start[i] != '\t') i++;
            size_t word_len = i - word_start;
            if (word_len == 0) continue;

            if (word_len > (size_t)width)
            {
                /* The word alone doesn't fit on any line (e.g. a long URL in
                 * an error message): flush what's pending, then hard-break
                 * the word itself into width-sized chunks. A rendered line
                 * wider than the terminal just gets silently clipped with no
                 * sign there was more, which is worse than a mid-word break. */
                if (current.len > 0)
                {
                    line_ptr_array_push(&lines, arena_strndup(arena, strbuf_cstr(&current), current.len));
                    strbuf_clear(&current);
                }

                size_t offset = 0;
                while (offset < word_len)
                {
                    size_t chunk = word_len - offset < (size_t)width ? word_len - offset : (size_t)width;
                    line_ptr_array_push(&lines, arena_strndup(arena, para_start + word_start + offset, chunk));
                    offset += chunk;
                }

                continue;
            }

            size_t needed = word_len + (current.len > 0 ? 1 : 0);
            if (current.len > 0 && current.len + needed > (size_t)width)
            {
                line_ptr_array_push(&lines, arena_strndup(arena, strbuf_cstr(&current), current.len));
                strbuf_clear(&current);
            }

            if (current.len > 0) strbuf_append_char(&current, ' ');
            strbuf_append(&current, para_start + word_start, word_len);
        }

        line_ptr_array_push(&lines, arena_strndup(arena, strbuf_cstr(&current), current.len));
        strbuf_clear(&current);

        if (*p == '\n') p++;
    }

    strbuf_free(&current);

    char **result = lines.count ? (char **)arena_alloc(arena, lines.count * sizeof(char *)) : NULL;
    for (size_t i = 0; i < lines.count; i++) result[i] = lines.items[i];

    *out_count = lines.count;
    line_ptr_array_free(&lines);
    return result;
}

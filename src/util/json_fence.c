#include "util/json_fence.h"

#include <string.h>

char *json_strip_code_fences(arena_t *arena, const char *text)
{
    const char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\n' || start[len - 1] == '\r')) len--;

    if (len >= 3 && strncmp(start, "```", 3) == 0)
    {
        const char *nl = memchr(start, '\n', len);
        if (nl)
        {
            size_t skip = (size_t)(nl - start) + 1;
            start += skip;
            len -= skip;
        }

        if (len >= 3 && strncmp(start + len - 3, "```", 3) == 0)
        {
            len -= 3;
            while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r')) len--;
        }
    }

    return arena_strndup(arena, start, len);
}

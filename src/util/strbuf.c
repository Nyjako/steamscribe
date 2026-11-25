#include "util/strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void strbuf_init(strbuf_t *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_free(strbuf_t *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

int strbuf_reserve(strbuf_t *sb, size_t min_cap)
{
    if (min_cap <= sb->cap) return 1;

    size_t new_cap = sb->cap ? sb->cap * 2 : 64;
    while (new_cap < min_cap) new_cap *= 2;

    char *new_data = (char *)realloc(sb->data, new_cap);
    if (!new_data) return 0;

    sb->data = new_data;
    sb->cap = new_cap;
    return 1;
}

int strbuf_append(strbuf_t *sb, const char *s, size_t n)
{
    if (!strbuf_reserve(sb, sb->len + n + 1)) return 0;

    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

int strbuf_append_cstr(strbuf_t *sb, const char *s)
{
    return strbuf_append(sb, s, strlen(s));
}

int strbuf_append_char(strbuf_t *sb, char c)
{
    return strbuf_append(sb, &c, 1);
}

int strbuf_append_fmt(strbuf_t *sb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) return 0;

    if (!strbuf_reserve(sb, sb->len + (size_t)needed + 1)) return 0;

    va_start(args, fmt);
    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, args);
    va_end(args);

    sb->len += (size_t)needed;
    return 1;
}

void strbuf_clear(strbuf_t *sb)
{
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

const char *strbuf_cstr(const strbuf_t *sb)
{
    return sb->data ? sb->data : "";
}

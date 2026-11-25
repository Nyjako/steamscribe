#ifndef STEAMSCRIBE_UTIL_STRBUF_H
#define STEAMSCRIBE_UTIL_STRBUF_H

#include <stddef.h>

typedef struct strbuf_t
{
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

void strbuf_init(strbuf_t *sb);
void strbuf_free(strbuf_t *sb);

/* Each of these returns 1 on success, 0 on allocation failure. */
int strbuf_reserve(strbuf_t *sb, size_t min_cap);
int strbuf_append(strbuf_t *sb, const char *s, size_t n);
int strbuf_append_cstr(strbuf_t *sb, const char *s);
int strbuf_append_char(strbuf_t *sb, char c);
int strbuf_append_fmt(strbuf_t *sb, const char *fmt, ...);

void strbuf_clear(strbuf_t *sb);

/* Always NUL-terminated; valid until the next mutating call. */
const char *strbuf_cstr(const strbuf_t *sb);

#endif

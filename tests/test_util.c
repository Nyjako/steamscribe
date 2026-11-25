#include "test_assert.h"

#include "util/arena.h"
#include "util/error.h"
#include "util/strbuf.h"

#include <string.h>

static int test_arena(void)
{
    arena_t arena;
    arena_init(&arena, 64);

    char *a = arena_strdup(&arena, "hello");
    TEST_ASSERT(strcmp(a, "hello") == 0);

    /* Bigger than the block size, forces a new block to be allocated. */
    char big[200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char *b = arena_strdup(&arena, big);
    TEST_ASSERT(strcmp(b, big) == 0);

    /* Earlier allocations stay valid after a new block is added. */
    TEST_ASSERT(strcmp(a, "hello") == 0);

    arena_free(&arena);
    return 0;
}

static int test_strbuf(void)
{
    strbuf_t sb;
    strbuf_init(&sb);

    TEST_ASSERT(strbuf_append_cstr(&sb, "foo"));
    TEST_ASSERT(strbuf_append_char(&sb, ' '));
    TEST_ASSERT(strbuf_append_fmt(&sb, "%s=%d", "bar", 42));
    TEST_ASSERT(strcmp(strbuf_cstr(&sb), "foo bar=42") == 0);

    strbuf_clear(&sb);
    TEST_ASSERT(strcmp(strbuf_cstr(&sb), "") == 0);

    strbuf_free(&sb);
    return 0;
}

static int test_error(void)
{
    err_t err;
    err_clear(&err);
    TEST_ASSERT(err.code == 0);

    err_set(&err, 42, "bad thing: %s", "oops");
    TEST_ASSERT(err.code == 42);
    TEST_ASSERT(strcmp(err.msg, "bad thing: oops") == 0);

    return 0;
}

int main(void)
{
    if (test_arena()) return 1;
    if (test_strbuf()) return 1;
    if (test_error()) return 1;

    printf("all tests passed\n");
    return 0;
}

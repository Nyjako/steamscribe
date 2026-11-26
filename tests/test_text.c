#include "test_assert.h"

#include "util/arena.h"
#include "util/text.h"

#include <stdio.h>
#include <string.h>

static int test_empty_text(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 123;
    char **lines = text_wrap(&arena, "", 10, &count);

    TEST_ASSERT(lines == NULL);
    TEST_ASSERT(count == 0);

    arena_free(&arena);
    return 0;
}

static int test_fits_on_one_line(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 0;
    char **lines = text_wrap(&arena, "hello world", 20, &count);

    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(lines[0], "hello world") == 0);

    arena_free(&arena);
    return 0;
}

static int test_wraps_at_width(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 0;
    char **lines = text_wrap(&arena, "the quick brown fox jumps", 10, &count);

    TEST_ASSERT(count == 3);
    TEST_ASSERT(strcmp(lines[0], "the quick") == 0);
    TEST_ASSERT(strcmp(lines[1], "brown fox") == 0);
    TEST_ASSERT(strcmp(lines[2], "jumps") == 0);

    arena_free(&arena);
    return 0;
}

static int test_overlong_word_is_hard_broken(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 0;
    char **lines = text_wrap(&arena, "a supercalifragilisticexpialidocious word", 10, &count);

    /* "supercalifragilisticexpialidocious" is 34 chars; at width 10 it
     * never fits on a line, so it's split into 10/10/10/4-char chunks
     * rather than overflowing the terminal width on its own line. */
    TEST_ASSERT(count == 6);
    TEST_ASSERT(strcmp(lines[0], "a") == 0);
    TEST_ASSERT(strcmp(lines[1], "supercalif") == 0);
    TEST_ASSERT(strcmp(lines[2], "ragilistic") == 0);
    TEST_ASSERT(strcmp(lines[3], "expialidoc") == 0);
    TEST_ASSERT(strcmp(lines[4], "ious") == 0);
    TEST_ASSERT(strcmp(lines[5], "word") == 0);

    arena_free(&arena);
    return 0;
}

static int test_explicit_newlines_are_hard_breaks(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 0;
    char **lines = text_wrap(&arena, "hello\n\nworld", 20, &count);

    TEST_ASSERT(count == 3);
    TEST_ASSERT(strcmp(lines[0], "hello") == 0);
    TEST_ASSERT(strcmp(lines[1], "") == 0);
    TEST_ASSERT(strcmp(lines[2], "world") == 0);

    arena_free(&arena);
    return 0;
}

static int test_trailing_newline_has_no_extra_blank_line(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 0;
    char **lines = text_wrap(&arena, "hello\n", 20, &count);

    TEST_ASSERT(count == 1);
    TEST_ASSERT(strcmp(lines[0], "hello") == 0);

    arena_free(&arena);
    return 0;
}

static int test_long_url_never_overflows_width(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    const char *msg = "Could not load game: HTTP 302 from "
                       "https://store.steampowered.com/appreviews/892970?json=1&filter=all&language=english&review_type=positive&purchase_type=all&num_per_page=100&cursor=%2A, "
                       "redirected to https://store.steampowered.com/ -- the request was likely blocked rather than reaching the API";

    size_t count = 0;
    char **lines = text_wrap(&arena, msg, 40, &count);

    TEST_ASSERT(count > 0);
    for (size_t i = 0; i < count; i++) TEST_ASSERT(strlen(lines[i]) <= 40);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_empty_text()) return 1;
    if (test_fits_on_one_line()) return 1;
    if (test_wraps_at_width()) return 1;
    if (test_overlong_word_is_hard_broken()) return 1;
    if (test_long_url_never_overflows_width()) return 1;
    if (test_explicit_newlines_are_hard_breaks()) return 1;
    if (test_trailing_newline_has_no_extra_blank_line()) return 1;

    printf("all tests passed\n");
    return 0;
}

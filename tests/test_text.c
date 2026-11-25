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

static int test_overlong_word_gets_its_own_line(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    size_t count = 0;
    char **lines = text_wrap(&arena, "a supercalifragilisticexpialidocious word", 10, &count);

    TEST_ASSERT(count == 3);
    TEST_ASSERT(strcmp(lines[0], "a") == 0);
    TEST_ASSERT(strcmp(lines[1], "supercalifragilisticexpialidocious") == 0);
    TEST_ASSERT(strcmp(lines[2], "word") == 0);

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

int main(void)
{
    if (test_empty_text()) return 1;
    if (test_fits_on_one_line()) return 1;
    if (test_wraps_at_width()) return 1;
    if (test_overlong_word_gets_its_own_line()) return 1;
    if (test_explicit_newlines_are_hard_breaks()) return 1;
    if (test_trailing_newline_has_no_extra_blank_line()) return 1;

    printf("all tests passed\n");
    return 0;
}

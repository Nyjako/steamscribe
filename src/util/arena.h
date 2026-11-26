#ifndef STEAMSCRIBE_UTIL_ARENA_H
#define STEAMSCRIBE_UTIL_ARENA_H

#include <stddef.h>

#define ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

typedef struct arena_block_t arena_block_t;

typedef struct arena_t
{
    arena_block_t *first;
    arena_block_t *current;
    size_t block_size;
} arena_t;

/* block_size is the size of each growth block; pass 0 for ARENA_DEFAULT_BLOCK_SIZE. */
void arena_init(arena_t *arena, size_t block_size);

/* Frees every block. The arena can be reused after this (as if just init'd). */
void arena_free(arena_t *arena);

void *arena_alloc(arena_t *arena, size_t size);
void *arena_alloc_zero(arena_t *arena, size_t size);

char *arena_strdup(arena_t *arena, const char *s);
char *arena_strndup(arena_t *arena, const char *s, size_t n);

#endif

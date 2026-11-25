#include "util/arena.h"

#include <stdlib.h>
#include <string.h>

struct arena_block_t
{
    arena_block_t *next;
    size_t size;
    size_t used;
    unsigned char *data;
};

static size_t align_up(size_t n, size_t align)
{
    return (n + (align - 1)) & ~(align - 1);
}

static arena_block_t *arena_block_create(size_t size)
{
    arena_block_t *block = (arena_block_t *)malloc(sizeof(arena_block_t));
    if (!block) return NULL;

    block->data = (unsigned char *)malloc(size);
    if (!block->data)
    {
        free(block);
        return NULL;
    }

    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

void arena_init(arena_t *arena, size_t block_size)
{
    arena->first = NULL;
    arena->current = NULL;
    arena->block_size = block_size ? block_size : ARENA_DEFAULT_BLOCK_SIZE;
}

void arena_free(arena_t *arena)
{
    arena_block_t *block = arena->first;
    while (block)
    {
        arena_block_t *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    arena->first = NULL;
    arena->current = NULL;
}

void *arena_alloc(arena_t *arena, size_t size)
{
    size_t aligned = align_up(size, sizeof(void *));

    if (arena->current && arena->current->used + aligned <= arena->current->size)
    {
        void *ptr = arena->current->data + arena->current->used;
        arena->current->used += aligned;
        return ptr;
    }

    size_t block_size = aligned > arena->block_size ? aligned : arena->block_size;
    arena_block_t *block = arena_block_create(block_size);
    if (!block) return NULL;

    if (arena->current) arena->current->next = block;
    else arena->first = block;
    arena->current = block;

    block->used = aligned;
    return block->data;
}

void *arena_alloc_zero(arena_t *arena, size_t size)
{
    void *ptr = arena_alloc(arena, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

char *arena_strndup(arena_t *arena, const char *s, size_t n)
{
    char *copy = (char *)arena_alloc(arena, n + 1);
    if (!copy) return NULL;
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *arena_strdup(arena_t *arena, const char *s)
{
    return arena_strndup(arena, s, strlen(s));
}

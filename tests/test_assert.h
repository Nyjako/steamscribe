#ifndef STEAMSCRIBE_TEST_ASSERT_H
#define STEAMSCRIBE_TEST_ASSERT_H

#include <stdio.h>

#define TEST_ASSERT(cond)                                                      \
do {                                                                           \
    if (!(cond))                                                               \
    {                                                                         \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        return 1;                                                             \
    }                                                                         \
} while (0)

#endif

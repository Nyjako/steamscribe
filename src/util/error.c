#include "util/error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void err_clear(err_t *err)
{
    if (!err) return;
    err->code = 0;
    err->msg[0] = '\0';
}

void err_set(err_t *err, int code, const char *fmt, ...)
{
    if (!err) return;
    err->code = code;

    va_list args;
    va_start(args, fmt);
    vsnprintf(err->msg, sizeof(err->msg), fmt, args);
    va_end(args);
}

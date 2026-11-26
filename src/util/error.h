#ifndef STEAMSCRIBE_UTIL_ERROR_H
#define STEAMSCRIBE_UTIL_ERROR_H

typedef struct err_t
{
    int code;
    char msg[512]; /* long enough for a message that embeds a full Steam API URL */
} err_t;

void err_clear(err_t *err);
void err_set(err_t *err, int code, const char *fmt, ...);

#endif

#include "util/debug_log.h"

#include <goblin/appdirs.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEBUG_LOG_APP_NAME "steamscribe"

static int debug_log_enabled = 0;

void debug_log_enable(void)
{
    debug_log_enabled = 1;
}

static char *debug_log_path(void)
{
    char *cache_dir = goblin_appdir_cache_dir(DEBUG_LOG_APP_NAME);
    if (!cache_dir) return NULL;

    if (goblin_appdir_mkdir_p(cache_dir) != 0)
    {
        free(cache_dir);
        return NULL;
    }

    char *path = goblin_appdir_join(cache_dir, "debug.log");
    free(cache_dir);
    return path;
}

void debug_logf(const char *fmt, ...)
{
    if (!debug_log_enabled) return;

    char *path = debug_log_path();
    if (!path) return;

    FILE *fp = fopen(path, "a");
    free(path);
    if (!fp) return;

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_buf);
    fprintf(fp, "[%s] ", timestamp);

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}

const char *debug_log_redact_key(const char *key)
{
    static char buf[64];

    if (!key || !key[0])
    {
        snprintf(buf, sizeof(buf), "(not set)");
        return buf;
    }

    size_t len = strlen(key);
    if (len <= 8)
    {
        snprintf(buf, sizeof(buf), "(%zu chars, redacted)", len);
        return buf;
    }

    snprintf(buf, sizeof(buf), "%.4s...%s (%zu chars)", key, key + len - 4, len);
    return buf;
}

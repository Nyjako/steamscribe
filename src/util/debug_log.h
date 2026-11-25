#ifndef STEAMSCRIBE_UTIL_DEBUG_LOG_H
#define STEAMSCRIBE_UTIL_DEBUG_LOG_H

/* Enables appending to ~/.cache/steamscribe/debug.log (respecting
 * XDG_CACHE_HOME). Meant to be called once, when --verbose is passed. */
void debug_log_enable(void);

/* No-op unless debug_log_enable() was called. printf-style; appends a
 * timestamped line to the debug log. Never pass a raw API key here --
 * redact it with debug_log_redact_key() first. */
void debug_logf(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* Returns a short, safe-to-log stand-in for an API key: its length and
 * first/last couple of characters, never the key itself. The result is a
 * pointer into a small internal static buffer, valid until the next call. */
const char *debug_log_redact_key(const char *key);

#endif

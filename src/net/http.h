#ifndef STEAMSCRIBE_NET_HTTP_H
#define STEAMSCRIBE_NET_HTTP_H

#include "util/error.h"
#include "util/strbuf.h"

typedef struct http_cfg_t
{
    long connect_timeout_s;
    long total_timeout_s;
    int max_retries;
} http_cfg_t;

http_cfg_t http_cfg_default(void);

/* GETs url, appending the response body to `out` (which is cleared first).
 * Retries on connection failures and 429/5xx responses, with backoff. */
int http_get(const http_cfg_t *cfg, const char *url, strbuf_t *out, err_t *err);

#endif

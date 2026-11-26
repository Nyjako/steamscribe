#ifndef STEAMSCRIBE_NET_HTTP_H
#define STEAMSCRIBE_NET_HTTP_H

#include <stddef.h>

#include "util/error.h"
#include "util/strbuf.h"

typedef struct http_cfg_t
{
    long connect_timeout_s;
    long total_timeout_s;
    int max_retries;
} http_cfg_t;

typedef struct http_header_t
{
    const char *name;
    const char *value;
} http_header_t;

http_cfg_t http_cfg_default(void);

/* GETs url, appending the response body to `out` (which is cleared first).
 * Retries on connection failures and 429/5xx responses, with backoff. */
int http_get(const http_cfg_t *cfg, const char *url, strbuf_t *out, err_t *err);

/* POSTs json_body to url with Content-Type: application/json plus any extra
 * headers, writing the response body to `out`. Same retry policy as http_get. */
int http_post_json(const http_cfg_t *cfg, const char *url, const http_header_t *headers, size_t header_count, const char *json_body, strbuf_t *out, err_t *err);

#endif

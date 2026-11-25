#include "net/http.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifndef STEAMSCRIBE_VERSION
#define STEAMSCRIBE_VERSION "0.0.0"
#endif

http_cfg_t http_cfg_default(void)
{
    http_cfg_t cfg;
    cfg.connect_timeout_s = 10;
    cfg.total_timeout_s = 30;
    cfg.max_retries = 2;
    return cfg;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    strbuf_t *out = (strbuf_t *)userdata;
    size_t n = size * nmemb;
    return strbuf_append(out, ptr, n) ? n : 0;
}

static void backoff_sleep(int attempt)
{
    long ms = 300L * attempt;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Performs `curl` (already configured for the method/url/body/headers of
 * this request) with the shared timeout/User-Agent/retry-on-429-or-5xx
 * policy, writing the response body to `out`. */
static int perform_with_retry(CURL *curl, const http_cfg_t *cfg, const char *url, strbuf_t *out, err_t *err)
{
    char user_agent[64];
    snprintf(user_agent, sizeof(user_agent), "steamscribe/%s", STEAMSCRIBE_VERSION);

    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, cfg->connect_timeout_s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, cfg->total_timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res;
    long status = 0;
    int attempt = 0;

    for (;;)
    {
        strbuf_clear(out);

        res = curl_easy_perform(curl);
        status = 0;
        if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        bool retryable = (res != CURLE_OK) || status == 429 || (status >= 500 && status < 600);
        if (!retryable || attempt >= cfg->max_retries) break;

        attempt++;
        backoff_sleep(attempt);
    }

    if (res != CURLE_OK)
    {
        err_set(err, (int)res, "request to %s failed: %s", url, curl_easy_strerror(res));
        return -1;
    }

    if (status < 200 || status >= 300)
    {
        err_set(err, (int)status, "HTTP %ld from %s", status, url);
        return -1;
    }

    return 0;
}

int http_get(const http_cfg_t *cfg, const char *url, strbuf_t *out, err_t *err)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        err_set(err, -1, "curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);

    int rc = perform_with_retry(curl, cfg, url, out, err);

    curl_easy_cleanup(curl);
    return rc;
}

int http_post_json(const http_cfg_t *cfg, const char *url, const http_header_t *headers, size_t header_count, const char *json_body, strbuf_t *out, err_t *err)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        err_set(err, -1, "curl_easy_init failed");
        return -1;
    }

    struct curl_slist *header_list = curl_slist_append(NULL, "Content-Type: application/json");
    for (size_t i = 0; i < header_count; i++)
    {
        char line[512];
        snprintf(line, sizeof(line), "%s: %s", headers[i].name, headers[i].value);
        header_list = curl_slist_append(header_list, line);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);

    int rc = perform_with_retry(curl, cfg, url, out, err);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return rc;
}

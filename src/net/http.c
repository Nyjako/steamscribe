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

    /* Deliberately not following redirects: every endpoint this app calls
     * returns JSON directly on success, so a 3xx here means the request
     * was rejected (e.g. redirected to a store HTML page) rather than
     * reaching the API -- surfacing that as a clear error beats silently
     * downloading and failing to parse the redirect target as JSON. */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

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
        /* These specific codes mean the request never reached the other
         * end at all -- worth calling out as "you're probably offline"
         * rather than a generic curl error, since that's the overwhelmingly
         * common cause. Anything else (e.g. a cert error, a malformed URL)
         * still gets curl's own message, which is already specific enough. */
        bool looks_offline = res == CURLE_COULDNT_RESOLVE_HOST || res == CURLE_COULDNT_RESOLVE_PROXY || res == CURLE_COULDNT_CONNECT ||
                              res == CURLE_OPERATION_TIMEDOUT || res == CURLE_SSL_CONNECT_ERROR;
        if (looks_offline) err_set(err, (int)res, "could not reach %s -- check your internet connection (%s)", url, curl_easy_strerror(res));
        else err_set(err, (int)res, "request to %s failed: %s", url, curl_easy_strerror(res));
        return -1;
    }

    if (status >= 300 && status < 400)
    {
        char *redirect_url = NULL;
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect_url);
        err_set(err, (int)status, "HTTP %ld, redirected to %s -- the request was likely blocked rather than reaching the API", status, redirect_url ? redirect_url : "(unknown)");
        return -1;
    }

    if (status == 401 || status == 403)
    {
        err_set(err, (int)status, "%s rejected the request (HTTP %ld) -- check that your API key is set and correct", url, status);
        return -1;
    }

    if (status == 429)
    {
        err_set(err, (int)status, "rate-limited by %s (HTTP 429) -- wait a bit and try again", url);
        return -1;
    }

    if (status >= 500 && status < 600)
    {
        err_set(err, (int)status, "%s is having trouble right now (HTTP %ld) -- try again shortly", url, status);
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

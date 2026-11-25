#include "steam/steam.h"

#include <cJSON.h>

#define GOBLIN_URL_IMPLEMENTATION
#include <goblin/url.h>

#define GOBLIN_CSTR_IMPLEMENTATION
#include <goblin/cstr.h>

#define GOBLIN_RATE_LIMIT_IMPLEMENTATION
#include <goblin/rate_limit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/http.h"
#include "util/strbuf.h"

#define REVIEW_MIN_CHARS 40
#define REVIEW_MAX_CHARS 1200

static char *join_descriptions(arena_t *arena, cJSON *array)
{
    strbuf_t sb;
    strbuf_init(&sb);

    if (cJSON_IsArray(array))
    {
        int n = cJSON_GetArraySize(array);
        for (int i = 0; i < n; i++)
        {
            cJSON *item = cJSON_GetArrayItem(array, i);
            cJSON *desc = cJSON_GetObjectItemCaseSensitive(item, "description");
            if (!cJSON_IsString(desc)) continue;
            if (sb.len) strbuf_append_cstr(&sb, ", ");
            strbuf_append_cstr(&sb, desc->valuestring);
        }
    }

    char *result = arena_strdup(arena, strbuf_cstr(&sb));
    strbuf_free(&sb);
    return result;
}

static char *join_strings(arena_t *arena, cJSON *array)
{
    strbuf_t sb;
    strbuf_init(&sb);

    if (cJSON_IsArray(array))
    {
        int n = cJSON_GetArraySize(array);
        for (int i = 0; i < n; i++)
        {
            cJSON *item = cJSON_GetArrayItem(array, i);
            if (!cJSON_IsString(item)) continue;
            if (sb.len) strbuf_append_cstr(&sb, ", ");
            strbuf_append_cstr(&sb, item->valuestring);
        }
    }

    char *result = arena_strdup(arena, strbuf_cstr(&sb));
    strbuf_free(&sb);
    return result;
}

int steam_parse_search(arena_t *arena, const char *json_text, steam_search_results_t *out, err_t *err)
{
    cJSON *root = cJSON_Parse(json_text);
    if (!root)
    {
        err_set(err, -1, "invalid search response JSON");
        return -1;
    }

    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(items))
    {
        cJSON_Delete(root);
        err_set(err, -1, "search response has no items array");
        return -1;
    }

    size_t count = (size_t)cJSON_GetArraySize(items);
    steam_search_result_t *results = count ? (steam_search_result_t *)arena_alloc(arena, count * sizeof(steam_search_result_t)) : NULL;

    for (size_t i = 0; i < count; i++)
    {
        cJSON *item = cJSON_GetArrayItem(items, (int)i);
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");

        results[i].appid = cJSON_IsNumber(id) ? (uint32_t)id->valuedouble : 0;
        results[i].name = cJSON_IsString(name) ? arena_strdup(arena, name->valuestring) : arena_strdup(arena, "");
    }

    out->items = results;
    out->count = count;

    cJSON_Delete(root);
    return 0;
}

int steam_parse_app_details(arena_t *arena, uint32_t appid, const char *json_text, game_info_t *out, err_t *err)
{
    cJSON *root = cJSON_Parse(json_text);
    if (!root)
    {
        err_set(err, -1, "invalid appdetails response JSON");
        return -1;
    }

    char key[16];
    snprintf(key, sizeof(key), "%u", appid);

    cJSON *entry = cJSON_GetObjectItemCaseSensitive(root, key);
    cJSON *success = entry ? cJSON_GetObjectItemCaseSensitive(entry, "success") : NULL;
    cJSON *data = entry ? cJSON_GetObjectItemCaseSensitive(entry, "data") : NULL;

    if (!cJSON_IsTrue(success) || !data)
    {
        cJSON_Delete(root);
        err_set(err, 404, "game %u not found", appid);
        return -1;
    }

    cJSON *name = cJSON_GetObjectItemCaseSensitive(data, "name");
    cJSON *short_desc = cJSON_GetObjectItemCaseSensitive(data, "short_description");
    cJSON *genres = cJSON_GetObjectItemCaseSensitive(data, "genres");
    cJSON *developers = cJSON_GetObjectItemCaseSensitive(data, "developers");
    cJSON *release_date = cJSON_GetObjectItemCaseSensitive(data, "release_date");
    cJSON *date = cJSON_IsObject(release_date) ? cJSON_GetObjectItemCaseSensitive(release_date, "date") : NULL;

    out->appid = appid;
    out->name = cJSON_IsString(name) ? arena_strdup(arena, name->valuestring) : arena_strdup(arena, "");
    out->short_desc = cJSON_IsString(short_desc) ? arena_strdup(arena, short_desc->valuestring) : arena_strdup(arena, "");
    out->genres = join_descriptions(arena, genres);
    out->developers = join_strings(arena, developers);
    out->release_date = cJSON_IsString(date) ? arena_strdup(arena, date->valuestring) : arena_strdup(arena, "");

    cJSON_Delete(root);
    return 0;
}

int steam_parse_reviews_page(arena_t *arena, const char *json_text, steam_reviews_page_t *out, err_t *err)
{
    cJSON *root = cJSON_Parse(json_text);
    if (!root)
    {
        err_set(err, -1, "invalid appreviews response JSON");
        return -1;
    }

    cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    if (!cJSON_IsNumber(success) || success->valueint != 1)
    {
        cJSON_Delete(root);
        err_set(err, -1, "appreviews response reports failure");
        return -1;
    }

    out->has_summary = false;
    out->score_desc = arena_strdup(arena, "");
    out->total_positive = 0;
    out->total_negative = 0;

    cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "query_summary");
    if (cJSON_IsObject(summary))
    {
        cJSON *score_desc = cJSON_GetObjectItemCaseSensitive(summary, "review_score_desc");
        cJSON *total_positive = cJSON_GetObjectItemCaseSensitive(summary, "total_positive");
        cJSON *total_negative = cJSON_GetObjectItemCaseSensitive(summary, "total_negative");

        out->has_summary = true;
        out->score_desc = cJSON_IsString(score_desc) ? arena_strdup(arena, score_desc->valuestring) : arena_strdup(arena, "");
        out->total_positive = cJSON_IsNumber(total_positive) ? total_positive->valueint : 0;
        out->total_negative = cJSON_IsNumber(total_negative) ? total_negative->valueint : 0;
    }

    cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
    out->cursor = cJSON_IsString(cursor) ? arena_strdup(arena, cursor->valuestring) : arena_strdup(arena, "");

    cJSON *reviews = cJSON_GetObjectItemCaseSensitive(root, "reviews");
    size_t count = cJSON_IsArray(reviews) ? (size_t)cJSON_GetArraySize(reviews) : 0;
    steam_review_t *items = count ? (steam_review_t *)arena_alloc(arena, count * sizeof(steam_review_t)) : NULL;

    for (size_t i = 0; i < count; i++)
    {
        cJSON *item = cJSON_GetArrayItem(reviews, (int)i);
        cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "review");
        cJSON *voted_up = cJSON_GetObjectItemCaseSensitive(item, "voted_up");
        cJSON *votes_up = cJSON_GetObjectItemCaseSensitive(item, "votes_up");
        cJSON *author = cJSON_GetObjectItemCaseSensitive(item, "author");
        cJSON *playtime = author ? cJSON_GetObjectItemCaseSensitive(author, "playtime_forever") : NULL;

        items[i].text = cJSON_IsString(text) ? arena_strdup(arena, text->valuestring) : arena_strdup(arena, "");
        items[i].voted_up = cJSON_IsTrue(voted_up);
        items[i].votes_up = cJSON_IsNumber(votes_up) ? votes_up->valueint : 0;
        items[i].playtime_min = cJSON_IsNumber(playtime) ? playtime->valueint : 0;
    }

    out->items = items;
    out->count = count;

    cJSON_Delete(root);
    return 0;
}

int steam_search(arena_t *arena, const char *query, const char *language, steam_search_results_t *out, err_t *err)
{
    char *encoded = goblin_url_encode(query);
    if (!encoded)
    {
        err_set(err, -1, "out of memory encoding search query");
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://store.steampowered.com/api/storesearch/?term=%s&cc=us&l=%s", encoded, language);
    free(encoded);

    http_cfg_t cfg = http_cfg_default();
    strbuf_t body;
    strbuf_init(&body);

    int rc = http_get(&cfg, url, &body, err);
    if (rc == 0) rc = steam_parse_search(arena, strbuf_cstr(&body), out, err);

    strbuf_free(&body);
    return rc;
}

int steam_app_details(arena_t *arena, uint32_t appid, const char *language, game_info_t *out, err_t *err)
{
    char url[256];
    snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%u&l=%s", appid, language);

    http_cfg_t cfg = http_cfg_default();
    strbuf_t body;
    strbuf_init(&body);

    int rc = http_get(&cfg, url, &body, err);
    if (rc == 0) rc = steam_parse_app_details(arena, appid, strbuf_cstr(&body), out, err);

    strbuf_free(&body);
    return rc;
}

int steam_reviews_summary(arena_t *arena, uint32_t appid, const char *language, review_set_t *out, err_t *err)
{
    char url[512];
    snprintf(url, sizeof(url),
             "https://store.steampowered.com/appreviews/%u?json=1&filter=all&language=%s&review_type=all&purchase_type=all&num_per_page=1&cursor=*",
             appid, language);

    http_cfg_t cfg = http_cfg_default();
    strbuf_t body;
    strbuf_init(&body);

    int rc = http_get(&cfg, url, &body, err);
    if (rc != 0)
    {
        strbuf_free(&body);
        return rc;
    }

    steam_reviews_page_t page;
    rc = steam_parse_reviews_page(arena, strbuf_cstr(&body), &page, err);
    strbuf_free(&body);
    if (rc != 0) return rc;

    out->items = NULL;
    out->count = 0;
    out->score_desc = page.score_desc;
    out->total_positive = page.total_positive;
    out->total_negative = page.total_negative;
    return 0;
}

static int fetch_reviews_type(arena_t *arena, uint32_t appid, const char *language, const char *review_type, size_t target, steam_review_t *dest, size_t *kept, review_set_t *summary_out, bool *have_summary, goblin_rate_limiter *limiter, err_t *err)
{
    http_cfg_t cfg = http_cfg_default();
    char *cursor = arena_strdup(arena, "*");

    for (;;)
    {
        char *encoded_cursor = goblin_url_encode(cursor);
        if (!encoded_cursor)
        {
            err_set(err, -1, "out of memory encoding review cursor");
            return -1;
        }

        char url[768];
        snprintf(url, sizeof(url),
                 "https://store.steampowered.com/appreviews/%u?json=1&filter=all&language=%s&review_type=%s&purchase_type=all&num_per_page=100&cursor=%s",
                 appid, language, review_type, encoded_cursor);
        free(encoded_cursor);

        goblin_rate_limiter_wait(limiter);

        strbuf_t body;
        strbuf_init(&body);
        int rc = http_get(&cfg, url, &body, err);
        if (rc != 0)
        {
            strbuf_free(&body);
            return rc;
        }

        steam_reviews_page_t page;
        rc = steam_parse_reviews_page(arena, strbuf_cstr(&body), &page, err);
        strbuf_free(&body);
        if (rc != 0) return rc;

        if (page.has_summary && !*have_summary)
        {
            summary_out->score_desc = page.score_desc;
            summary_out->total_positive = page.total_positive;
            summary_out->total_negative = page.total_negative;
            *have_summary = true;
        }

        bool page_empty = (page.count == 0);
        bool cursor_unchanged = strcmp(cursor, page.cursor) == 0;

        for (size_t i = 0; i < page.count; i++)
        {
            if (*kept >= target) break;

            goblin_trim_cstr(page.items[i].text);
            size_t len = strlen(page.items[i].text);
            if (len < REVIEW_MIN_CHARS) continue;
            if (len > REVIEW_MAX_CHARS) page.items[i].text[REVIEW_MAX_CHARS] = '\0';

            dest[*kept] = page.items[i];
            (*kept)++;
        }

        cursor = page.cursor;

        if (*kept >= target || page_empty || cursor_unchanged) break;
    }

    return 0;
}

int steam_reviews_sample(arena_t *arena, uint32_t appid, const char *language, size_t count, review_set_t *out, err_t *err)
{
    size_t target_per_type = count / 2;

    steam_review_t *items = count ? (steam_review_t *)arena_alloc(arena, count * sizeof(steam_review_t)) : NULL;
    size_t kept = 0;

    review_set_t summary;
    summary.score_desc = arena_strdup(arena, "");
    summary.total_positive = 0;
    summary.total_negative = 0;
    bool have_summary = false;

    goblin_rate_limiter limiter;
    goblin_rate_limiter_init(&limiter, 0.3);

    int rc = fetch_reviews_type(arena, appid, language, "positive", target_per_type, items, &kept, &summary, &have_summary, &limiter, err);
    if (rc != 0) return rc;

    size_t negative_target = kept + (count - target_per_type);
    if (negative_target > count) negative_target = count;

    rc = fetch_reviews_type(arena, appid, language, "negative", negative_target, items, &kept, &summary, &have_summary, &limiter, err);
    if (rc != 0) return rc;

    out->items = items;
    out->count = kept;
    out->score_desc = summary.score_desc;
    out->total_positive = summary.total_positive;
    out->total_negative = summary.total_negative;
    return 0;
}

#include "test_assert.h"

#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_fixture(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURES_DIR, name);

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, fp);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

static int test_parse_search(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *json = read_fixture("storesearch_valheim.json");
    TEST_ASSERT(json != NULL);

    steam_search_results_t results;
    err_t err;
    int rc = steam_parse_search(&arena, json, &results, &err);
    free(json);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(results.count == 2);
    TEST_ASSERT(results.items[0].appid == 892970);
    TEST_ASSERT(strcmp(results.items[0].name, "Valheim") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_app_details(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *json = read_fixture("appdetails_892970.json");
    TEST_ASSERT(json != NULL);

    game_info_t info;
    err_t err;
    int rc = steam_parse_app_details(&arena, 892970, json, &info, &err);
    free(json);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(info.name, "Valheim") == 0);
    TEST_ASSERT(strstr(info.genres, "Action") != NULL);
    TEST_ASSERT(strstr(info.genres, "RPG") != NULL);
    TEST_ASSERT(strcmp(info.developers, "Iron Gate AB") == 0);
    TEST_ASSERT(strcmp(info.release_date, "9 Sep, 2026") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_app_details_not_found(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *json = read_fixture("appdetails_not_found.json");
    TEST_ASSERT(json != NULL);

    game_info_t info;
    err_t err;
    int rc = steam_parse_app_details(&arena, 0, json, &info, &err);
    free(json);

    TEST_ASSERT(rc != 0);
    TEST_ASSERT(err.code == 404);

    arena_free(&arena);
    return 0;
}

static int test_parse_reviews_page_positive(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *json = read_fixture("getappreviews_892970_positive_page1.json");
    TEST_ASSERT(json != NULL);

    steam_reviews_page_t page;
    err_t err;
    int rc = steam_parse_reviews_page(&arena, json, &page, &err);
    free(json);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(page.has_summary);
    TEST_ASSERT(strcmp(page.score_desc, "Overwhelmingly Positive") == 0);
    TEST_ASSERT(page.total_positive == 325000);
    TEST_ASSERT(page.total_negative == 12000);
    TEST_ASSERT(page.count == 3);
    TEST_ASSERT(page.items[0].voted_up);
    TEST_ASSERT(page.items[0].votes_up == 42);
    TEST_ASSERT(page.items[0].playtime_min == 6000);
    TEST_ASSERT(strcmp(page.cursor, "AoJ4rpWS0PACcN+2wQI=") == 0);

    /* The short "good" review is kept as-is by the parser -- length
     * filtering happens in steam_reviews_sample, not here. */
    TEST_ASSERT(strcmp(page.items[1].text, "good") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_reviews_page_negative(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *json = read_fixture("getappreviews_892970_negative_page1.json");
    TEST_ASSERT(json != NULL);

    steam_reviews_page_t page;
    err_t err;
    int rc = steam_parse_reviews_page(&arena, json, &page, &err);
    free(json);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(page.count == 2);
    TEST_ASSERT(!page.items[0].voted_up);

    arena_free(&arena);
    return 0;
}

static int test_parse_reviews_page_second_page_has_no_summary(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *json = read_fixture("getappreviews_892970_page2_no_score.json");
    TEST_ASSERT(json != NULL);

    steam_reviews_page_t page;
    err_t err;
    int rc = steam_parse_reviews_page(&arena, json, &page, &err);
    free(json);

    /* query_summary is present on every page, but with only num_reviews --
     * no review_score_desc means has_summary must stay false. */
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!page.has_summary);
    TEST_ASSERT(page.count == 1);

    arena_free(&arena);
    return 0;
}

static int test_parse_reviews_page_empty_response(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    /* GetAppReviews returns HTTP 200 with an empty "response" object (no
     * "success" field at all) for e.g. an unknown appid. */
    char *json = read_fixture("getappreviews_empty.json");
    TEST_ASSERT(json != NULL);

    steam_reviews_page_t page;
    err_t err;
    int rc = steam_parse_reviews_page(&arena, json, &page, &err);
    free(json);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(!page.has_summary);
    TEST_ASSERT(page.count == 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_parse_search()) return 1;
    if (test_parse_app_details()) return 1;
    if (test_parse_app_details_not_found()) return 1;
    if (test_parse_reviews_page_positive()) return 1;
    if (test_parse_reviews_page_negative()) return 1;
    if (test_parse_reviews_page_second_page_has_no_summary()) return 1;
    if (test_parse_reviews_page_empty_response()) return 1;

    printf("all tests passed\n");
    return 0;
}

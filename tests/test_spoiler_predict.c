#include "test_assert.h"

#include "core/spoiler_predict.h"
#include "llm/fake.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <string.h>

static llm_cfg_t make_fake_cfg(void)
{
    llm_cfg_t cfg;
    cfg.model = "fake-model";
    cfg.base_url = "";
    cfg.api_key = "";
    cfg.num_ctx = 0;
    return cfg;
}

static int test_build_prompt_includes_notes_and_answers(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    const char *answers[] = {"The final boss turns out to be your own brother.", ""};

    char *system = NULL;
    char *user = NULL;
    spoiler_predict_build_prompt(&arena, "Valheim", "The ending made me cry.", answers, 2, &system, &user);

    TEST_ASSERT(strstr(system, "\"spoiler\"") != NULL);
    TEST_ASSERT(strstr(user, "Valheim") != NULL);
    TEST_ASSERT(strstr(user, "The ending made me cry.") != NULL);
    TEST_ASSERT(strstr(user, "final boss turns out to be your own brother") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_build_prompt_empty_notes_and_answers(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *system = NULL;
    char *user = NULL;
    spoiler_predict_build_prompt(&arena, "Valheim", "", NULL, 0, &system, &user);

    TEST_ASSERT(strstr(user, "(none)") != NULL);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_plain(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_parse_response(&arena, "{\"spoiler\":\"The final boss is your own brother.\"}", &spoiler, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(spoiler, "The final boss is your own brother.") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_empty_is_valid(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_parse_response(&arena, "{\"spoiler\":\"\"}", &spoiler, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(spoiler, "") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_fenced(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_parse_response(&arena, "```json\n{\"spoiler\":\"It was all a dream.\"}\n```", &spoiler, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(spoiler, "It was all a dream.") == 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_wrong_key(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_parse_response(&arena, "{\"notes\":\"\"}", &spoiler, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_parse_response_invalid_json(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_parse_response(&arena, "not json at all", &spoiler, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

static int test_generate_uses_fake_provider(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    llm_cfg_t cfg = make_fake_cfg();
    llm_fake_set_response("{\"spoiler\":\"The ending made me cry.\"}");

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_generate(&arena, &llm_provider_fake, &cfg, "Valheim", "The ending made me cry.", NULL, 0, &spoiler, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(spoiler, "The ending made me cry.") == 0);

    arena_free(&arena);
    return 0;
}

static int test_generate_retries_then_succeeds(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    llm_cfg_t cfg = make_fake_cfg();

    const char *responses[2] = {
        "not valid json",
        "{\"spoiler\":\"\"}",
    };
    llm_fake_set_response_sequence(responses, 2);

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_generate(&arena, &llm_provider_fake, &cfg, "Valheim", "Loved it.", NULL, 0, &spoiler, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(spoiler, "") == 0);

    arena_free(&arena);
    return 0;
}

static int test_generate_fails_after_retry_exhausted(void)
{
    arena_t arena;
    arena_init(&arena, 0);

    llm_cfg_t cfg = make_fake_cfg();
    llm_fake_set_response("still not json");

    char *spoiler = NULL;
    err_t err;
    int rc = spoiler_predict_generate(&arena, &llm_provider_fake, &cfg, "Valheim", "Loved it.", NULL, 0, &spoiler, &err);

    TEST_ASSERT(rc != 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_build_prompt_includes_notes_and_answers()) return 1;
    if (test_build_prompt_empty_notes_and_answers()) return 1;
    if (test_parse_response_plain()) return 1;
    if (test_parse_response_empty_is_valid()) return 1;
    if (test_parse_response_fenced()) return 1;
    if (test_parse_response_wrong_key()) return 1;
    if (test_parse_response_invalid_json()) return 1;
    if (test_generate_uses_fake_provider()) return 1;
    if (test_generate_retries_then_succeeds()) return 1;
    if (test_generate_fails_after_retry_exhausted()) return 1;

    printf("all tests passed\n");
    return 0;
}

#include "test_assert.h"

#include "config.h"
#include "core/pipeline.h"
#include "steam/steam.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <string.h>

static config_t make_cfg(const char *provider, const char *base_url, const char *anthropic_key, const char *openai_key)
{
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.llm_provider = (char *)provider;
    cfg.llm_model = (char *)"some-model";
    cfg.llm_base_url = (char *)base_url;
    cfg.llm_num_ctx = 0;
    cfg.anthropic_api_key = (char *)anthropic_key;
    cfg.openai_api_key = (char *)openai_key;
    return cfg;
}

static int test_anthropic_without_key_fails_fast(void)
{
    config_t cfg = make_cfg("anthropic", "", "", "");

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_resolve_llm(&cfg, &provider, &llm_cfg, &err) != 0);
    TEST_ASSERT(strstr(err.msg, "ANTHROPIC_API_KEY") != NULL);
    return 0;
}

static int test_anthropic_with_key_resolves(void)
{
    config_t cfg = make_cfg("anthropic", "", "sk-test", "");

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_resolve_llm(&cfg, &provider, &llm_cfg, &err) == 0);
    TEST_ASSERT(strcmp(provider->name, "anthropic") == 0);
    TEST_ASSERT(strcmp(llm_cfg.api_key, "sk-test") == 0);
    return 0;
}

static int test_openai_without_key_against_real_api_fails_fast(void)
{
    /* No base_url => the real api.openai.com, which does require a key. */
    config_t cfg = make_cfg("openai", "", "", "");

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_resolve_llm(&cfg, &provider, &llm_cfg, &err) != 0);
    TEST_ASSERT(strstr(err.msg, "OPENAI_API_KEY") != NULL);
    return 0;
}

static int test_openai_without_key_against_local_server_is_fine(void)
{
    /* A base_url pointing at a local server (LM Studio, llama-server, ...)
     * is the documented no-key case -- must not be blocked. */
    config_t cfg = make_cfg("openai", "http://localhost:1234/v1", "", "");

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_resolve_llm(&cfg, &provider, &llm_cfg, &err) == 0);
    TEST_ASSERT(strcmp(provider->name, "openai") == 0);
    TEST_ASSERT(llm_cfg.api_key[0] == '\0');
    return 0;
}

static int test_ollama_never_needs_anthropic_or_openai_keys(void)
{
    config_t cfg = make_cfg("ollama", "", "", "");

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_resolve_llm(&cfg, &provider, &llm_cfg, &err) == 0);
    TEST_ASSERT(strcmp(provider->name, "ollama") == 0);
    return 0;
}

static int test_unknown_provider_errors(void)
{
    config_t cfg = make_cfg("not-a-real-provider", "", "", "");

    const llm_provider_t *provider;
    llm_cfg_t llm_cfg;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_resolve_llm(&cfg, &provider, &llm_cfg, &err) != 0);
    return 0;
}

static int test_fetch_library_without_steam_id_or_key_fails_fast(void)
{
    config_t cfg = make_cfg("anthropic", "", "sk-test", "");
    cfg.steam_id = (char *)"";
    cfg.steam_api_key = (char *)"";

    arena_t arena;
    arena_init(&arena, 0);

    steam_library_t library;
    err_t err;
    err_clear(&err);

    TEST_ASSERT(pipeline_fetch_library(&arena, &cfg, &library, &err) != 0);
    TEST_ASSERT(strstr(err.msg, "Settings") != NULL);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_anthropic_without_key_fails_fast()) return 1;
    if (test_anthropic_with_key_resolves()) return 1;
    if (test_openai_without_key_against_real_api_fails_fast()) return 1;
    if (test_openai_without_key_against_local_server_is_fine()) return 1;
    if (test_ollama_never_needs_anthropic_or_openai_keys()) return 1;
    if (test_unknown_provider_errors()) return 1;
    if (test_fetch_library_without_steam_id_or_key_fails_fast()) return 1;

    printf("test_pipeline: all tests passed\n");
    return 0;
}

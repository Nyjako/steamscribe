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

static int g_fake_unload_calls = 0;
static int g_fake_unload_rc = 0;

static int fake_unload(const llm_cfg_t *cfg, err_t *err)
{
    (void)cfg;
    (void)err;
    g_fake_unload_calls++;
    return g_fake_unload_rc;
}

static int test_unload_llm_model_calls_through_when_supported(void)
{
    g_fake_unload_calls = 0;
    g_fake_unload_rc = 0;

    llm_provider_t provider = {"fake-with-unload", NULL, NULL, fake_unload};
    llm_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model = "some-model";

    pipeline_unload_llm_model(&provider, &cfg);
    TEST_ASSERT(g_fake_unload_calls == 1);
    return 0;
}

static int test_unload_llm_model_is_a_noop_when_unsupported(void)
{
    g_fake_unload_calls = 0;

    llm_provider_t provider = {"fake-without-unload", NULL, NULL, NULL};
    llm_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model = "some-model";

    pipeline_unload_llm_model(&provider, &cfg); /* must not crash, must not call fake_unload */
    TEST_ASSERT(g_fake_unload_calls == 0);
    return 0;
}

static int test_unload_llm_model_swallows_a_failure(void)
{
    g_fake_unload_calls = 0;
    g_fake_unload_rc = -1;

    llm_provider_t provider = {"fake-with-unload", NULL, NULL, fake_unload};
    llm_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.model = "some-model";

    pipeline_unload_llm_model(&provider, &cfg); /* best-effort: a failure is logged, never fatal */
    TEST_ASSERT(g_fake_unload_calls == 1);

    g_fake_unload_rc = 0;
    return 0;
}

static int test_test_llm_connection_uses_staged_values_not_cfg(void)
{
    /* cfg itself is configured for ollama (no key needed) -- the staged
     * provider_name "anthropic" passed in must be what actually gets
     * tested, not cfg->llm_provider, and anthropic has no key configured
     * here so it must fail fast before ever reaching the network. */
    config_t cfg = make_cfg("ollama", "", "", "");

    arena_t arena;
    arena_init(&arena, 0);

    char *reply = NULL;
    err_t err;
    err_clear(&err);

    int rc = pipeline_test_llm_connection(&arena, &cfg, "anthropic", "claude-haiku-5-5", "", 0, &reply, &err);
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(strstr(err.msg, "ANTHROPIC_API_KEY") != NULL);

    arena_free(&arena);
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
    if (test_test_llm_connection_uses_staged_values_not_cfg()) return 1;
    if (test_fetch_library_without_steam_id_or_key_fails_fast()) return 1;
    if (test_unload_llm_model_calls_through_when_supported()) return 1;
    if (test_unload_llm_model_is_a_noop_when_unsupported()) return 1;
    if (test_unload_llm_model_swallows_a_failure()) return 1;

    printf("test_pipeline: all tests passed\n");
    return 0;
}

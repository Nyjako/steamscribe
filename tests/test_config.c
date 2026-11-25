#include "test_assert.h"

#include "config.h"
#include "util/arena.h"
#include "util/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_defaults_and_env_key(void)
{
    setenv("XDG_CONFIG_HOME", TEST_TMP_DIR "/config_defaults", 1);
    setenv("ANTHROPIC_API_KEY", "test-key-123", 1);
    unsetenv("OPENAI_API_KEY");

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    int rc = config_load(&arena, &cfg, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(cfg.llm_provider, "anthropic") == 0);
    TEST_ASSERT(strcmp(cfg.steam_language, "english") == 0);
    TEST_ASSERT(cfg.steam_review_count == 200);
    TEST_ASSERT(cfg.steam_cache_ttl_hours == 24);
    TEST_ASSERT(strcmp(cfg.review_default_length, "short") == 0);
    TEST_ASSERT(strcmp(cfg.anthropic_api_key, "test-key-123") == 0);
    TEST_ASSERT(strcmp(cfg.openai_api_key, "") == 0);

    char path[512];
    snprintf(path, sizeof(path), "%s/config_defaults/steamscribe/config.ini", TEST_TMP_DIR);
    FILE *fp = fopen(path, "r");
    TEST_ASSERT(fp != NULL);
    fclose(fp);

    arena_free(&arena);
    return 0;
}

static int test_reads_existing_values(void)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/config_existing/steamscribe", TEST_TMP_DIR);

    char mkdir_cmd[600];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", dir);
    TEST_ASSERT(system(mkdir_cmd) == 0);

    char path[600];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    FILE *fp = fopen(path, "w");
    TEST_ASSERT(fp != NULL);
    fputs("[llm]\nprovider = openai\nmodel = llama3\nbase_url = http://localhost:11434/v1\n", fp);
    fclose(fp);

    char config_home[512];
    snprintf(config_home, sizeof(config_home), "%s/config_existing", TEST_TMP_DIR);
    setenv("XDG_CONFIG_HOME", config_home, 1);

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    int rc = config_load(&arena, &cfg, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(cfg.llm_provider, "openai") == 0);
    TEST_ASSERT(strcmp(cfg.llm_model, "llama3") == 0);
    TEST_ASSERT(strcmp(cfg.llm_base_url, "http://localhost:11434/v1") == 0);

    arena_free(&arena);
    return 0;
}

int main(void)
{
    if (test_defaults_and_env_key()) return 1;
    if (test_reads_existing_values()) return 1;

    printf("all tests passed\n");
    return 0;
}

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
    unsetenv("STEAM_API_KEY");

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
    TEST_ASSERT(strcmp(cfg.steam_id, "") == 0);
    TEST_ASSERT(strcmp(cfg.review_default_length, "short") == 0);
    TEST_ASSERT(strcmp(cfg.review_default_vocab, "normal") == 0);
    TEST_ASSERT(cfg.llm_num_ctx == 8192);
    TEST_ASSERT(strcmp(cfg.anthropic_api_key, "test-key-123") == 0);
    TEST_ASSERT(strcmp(cfg.openai_api_key, "") == 0);
    TEST_ASSERT(strcmp(cfg.steam_api_key, "") == 0);

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
    fputs("[llm]\nprovider = ollama\nmodel = llama3\nbase_url = http://localhost:11434\nnum_ctx = 16384\n", fp);
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
    TEST_ASSERT(strcmp(cfg.llm_provider, "ollama") == 0);
    TEST_ASSERT(strcmp(cfg.llm_model, "llama3") == 0);
    TEST_ASSERT(strcmp(cfg.llm_base_url, "http://localhost:11434") == 0);
    TEST_ASSERT(cfg.llm_num_ctx == 16384);

    arena_free(&arena);
    return 0;
}

static int test_steam_id_and_api_key(void)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/config_steam_id/steamscribe", TEST_TMP_DIR);

    char mkdir_cmd[600];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", dir);
    TEST_ASSERT(system(mkdir_cmd) == 0);

    char path[600];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    FILE *fp = fopen(path, "w");
    TEST_ASSERT(fp != NULL);
    fputs("[steam]\nsteam_id = 76561198000000000\n", fp);
    fclose(fp);

    char config_home[512];
    snprintf(config_home, sizeof(config_home), "%s/config_steam_id", TEST_TMP_DIR);
    setenv("XDG_CONFIG_HOME", config_home, 1);
    setenv("STEAM_API_KEY", "test-steam-key", 1);

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    int rc = config_load(&arena, &cfg, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(cfg.steam_id, "76561198000000000") == 0);
    TEST_ASSERT(strcmp(cfg.steam_api_key, "test-steam-key") == 0);

    unsetenv("STEAM_API_KEY");
    arena_free(&arena);
    return 0;
}

static int test_save_then_reload_round_trips(void)
{
    char config_home[512];
    snprintf(config_home, sizeof(config_home), "%s/config_save_round_trip", TEST_TMP_DIR);
    setenv("XDG_CONFIG_HOME", config_home, 1);

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    TEST_ASSERT(config_load(&arena, &cfg, &err) == 0);

    cfg.llm_provider = arena_strdup(&arena, "ollama");
    cfg.llm_model = arena_strdup(&arena, "qwen2.5:14b");
    cfg.llm_base_url = arena_strdup(&arena, "http://localhost:11434");
    cfg.llm_num_ctx = 16384;
    cfg.review_default_vocab = arena_strdup(&arena, "simple");

    TEST_ASSERT(config_save(&cfg, &err) == 0);

    arena_t arena2;
    arena_init(&arena2, 0);

    config_t reloaded;
    TEST_ASSERT(config_load(&arena2, &reloaded, &err) == 0);

    TEST_ASSERT(strcmp(reloaded.llm_provider, "ollama") == 0);
    TEST_ASSERT(strcmp(reloaded.llm_model, "qwen2.5:14b") == 0);
    TEST_ASSERT(strcmp(reloaded.llm_base_url, "http://localhost:11434") == 0);
    TEST_ASSERT(reloaded.llm_num_ctx == 16384);
    TEST_ASSERT(strcmp(reloaded.review_default_vocab, "simple") == 0);
    /* unrelated sections survive the round trip unchanged */
    TEST_ASSERT(strcmp(reloaded.steam_language, "english") == 0);
    TEST_ASSERT(reloaded.steam_review_count == 200);
    TEST_ASSERT(strcmp(reloaded.steam_id, "") == 0);

    arena_free(&arena);
    arena_free(&arena2);
    return 0;
}

int main(void)
{
    if (test_defaults_and_env_key()) return 1;
    if (test_reads_existing_values()) return 1;
    if (test_steam_id_and_api_key()) return 1;
    if (test_save_then_reload_round_trips()) return 1;

    printf("all tests passed\n");
    return 0;
}

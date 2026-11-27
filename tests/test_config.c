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

    /* TEST_TMP_DIR survives between local test runs (only a clean build
     * wipes it), so without this a leftover config.ini from before a
     * DEFAULT_CONFIG_INI change would get silently reused instead of
     * regenerated, making this "first run" test not actually exercise a
     * first run. CI always starts from a clean checkout, so it never hit
     * this -- only a local rerun after editing the default template would. */
    remove(TEST_TMP_DIR "/config_defaults/steamscribe/config.ini");

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    int rc = config_load(&arena, &cfg, &err);

    TEST_ASSERT(rc == 0);
    TEST_ASSERT(strcmp(cfg.llm_provider, "ollama") == 0);
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
    TEST_ASSERT(cfg.steam_api_key_from_env == true);

    unsetenv("STEAM_API_KEY");
    arena_free(&arena);
    return 0;
}

static char *read_whole_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    static char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

static int test_plaintext_steam_api_key_is_encrypted_on_load(void)
{
    unsetenv("STEAM_API_KEY");

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/config_plain_key/steamscribe", TEST_TMP_DIR);
    char mkdir_cmd[600];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", dir);
    TEST_ASSERT(system(mkdir_cmd) == 0);

    char path[600];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    FILE *fp = fopen(path, "w");
    TEST_ASSERT(fp != NULL);
    fputs("[steam]\nsteam_api_key = my-plain-steam-key\n", fp);
    fclose(fp);

    char config_home[512];
    snprintf(config_home, sizeof(config_home), "%s/config_plain_key", TEST_TMP_DIR);
    setenv("XDG_CONFIG_HOME", config_home, 1);
    setenv("XDG_DATA_HOME", TEST_TMP_DIR "/data_plain_key", 1);

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    TEST_ASSERT(config_load(&arena, &cfg, &err) == 0);

    /* usable this run, in plaintext, even though it was just pasted in */
    TEST_ASSERT(strcmp(cfg.steam_api_key, "my-plain-steam-key") == 0);
    TEST_ASSERT(cfg.steam_api_key_from_env == false);

    /* and the file on disk no longer has it in plaintext */
    char *on_disk = read_whole_file(path);
    TEST_ASSERT(on_disk != NULL);
    TEST_ASSERT(strstr(on_disk, "my-plain-steam-key") == NULL);
    TEST_ASSERT(strstr(on_disk, "steam_api_key = enc1:") != NULL);

    /* a second load decrypts it back to the same plaintext, and leaves
     * the already-encrypted file alone */
    arena_t arena2;
    arena_init(&arena2, 0);
    config_t reloaded;
    TEST_ASSERT(config_load(&arena2, &reloaded, &err) == 0);
    TEST_ASSERT(strcmp(reloaded.steam_api_key, "my-plain-steam-key") == 0);

    char *on_disk_again = read_whole_file(path);
    TEST_ASSERT(on_disk_again != NULL);
    TEST_ASSERT(strcmp(on_disk, on_disk_again) == 0);

    arena_free(&arena);
    arena_free(&arena2);
    return 0;
}

static int test_env_steam_api_key_never_written_to_file(void)
{
    char config_home[512];
    snprintf(config_home, sizeof(config_home), "%s/config_env_key_not_saved", TEST_TMP_DIR);
    setenv("XDG_CONFIG_HOME", config_home, 1);
    setenv("XDG_DATA_HOME", TEST_TMP_DIR "/data_env_key_not_saved", 1);
    setenv("STEAM_API_KEY", "env-sourced-key", 1);

    arena_t arena;
    arena_init(&arena, 0);

    config_t cfg;
    err_t err;
    TEST_ASSERT(config_load(&arena, &cfg, &err) == 0);
    TEST_ASSERT(cfg.steam_api_key_from_env == true);

    TEST_ASSERT(config_save(&cfg, &err) == 0);

    char path[600];
    snprintf(path, sizeof(path), "%s/steamscribe/config.ini", config_home);
    char *on_disk = read_whole_file(path);
    TEST_ASSERT(on_disk != NULL);
    TEST_ASSERT(strstr(on_disk, "env-sourced-key") == NULL);
    TEST_ASSERT(strstr(on_disk, "enc1:") == NULL);

    unsetenv("STEAM_API_KEY");
    arena_free(&arena);
    return 0;
}

static int test_save_then_reload_round_trips(void)
{
    unsetenv("STEAM_API_KEY");

    char config_home[512];
    snprintf(config_home, sizeof(config_home), "%s/config_save_round_trip", TEST_TMP_DIR);
    setenv("XDG_CONFIG_HOME", config_home, 1);
    setenv("XDG_DATA_HOME", TEST_TMP_DIR "/data_save_round_trip", 1);

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
    cfg.steam_api_key = arena_strdup(&arena, "typed-in-settings-key"); /* steam_api_key_from_env already false */

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
    TEST_ASSERT(strcmp(reloaded.steam_api_key, "typed-in-settings-key") == 0);
    TEST_ASSERT(reloaded.steam_api_key_from_env == false);
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
    if (test_plaintext_steam_api_key_is_encrypted_on_load()) return 1;
    if (test_env_steam_api_key_never_written_to_file()) return 1;
    if (test_save_then_reload_round_trips()) return 1;

    printf("all tests passed\n");
    return 0;
}

#include "steam/cache.h"

#define GOBLIN_APPDIR_IMPLEMENTATION
#include <goblin/appdirs.h>

#define GOBLIN_FILEIO_IMPLEMENTATION
#include <goblin/fileio.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define CACHE_APP_NAME "steamscribe"

static char *cache_appid_dir(uint32_t appid)
{
    char *cache_dir = goblin_appdir_cache_dir(CACHE_APP_NAME);
    if (!cache_dir) return NULL;

    char appid_str[16];
    snprintf(appid_str, sizeof(appid_str), "%u", appid);

    char *appid_dir = goblin_appdir_join(cache_dir, appid_str);
    free(cache_dir);
    return appid_dir;
}

static char *cache_entry_path(const char *appid_dir, const char *name)
{
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.json", name);
    return goblin_appdir_join(appid_dir, filename);
}

int cache_get(arena_t *arena, uint32_t appid, const char *name, int ttl_hours, char **out_json, err_t *err)
{
    *out_json = NULL;

    char *dir = cache_appid_dir(appid);
    if (!dir)
    {
        err_set(err, -1, "could not build cache path for appid %u", appid);
        return -1;
    }

    char *path = cache_entry_path(dir, name);
    free(dir);
    if (!path)
    {
        err_set(err, -1, "could not build cache path for appid %u", appid);
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0)
    {
        free(path);
        return 0;
    }

    double age_hours = difftime(time(NULL), st.st_mtime) / 3600.0;
    if (age_hours > (double)ttl_hours)
    {
        free(path);
        return 0;
    }

    size_t len = 0;
    char *contents = goblin_read_file(path, &len);
    free(path);
    if (!contents) return 0;

    *out_json = arena_strndup(arena, contents, len);
    free(contents);
    return *out_json ? 1 : -1;
}

int cache_put(uint32_t appid, const char *name, const char *json, err_t *err)
{
    char *dir = cache_appid_dir(appid);
    if (!dir)
    {
        err_set(err, -1, "could not build cache path for appid %u", appid);
        return -1;
    }

    if (goblin_appdir_mkdir_p(dir) != 0)
    {
        err_set(err, -1, "could not create cache dir %s", dir);
        free(dir);
        return -1;
    }

    char *path = cache_entry_path(dir, name);
    free(dir);
    if (!path)
    {
        err_set(err, -1, "could not build cache path for appid %u", appid);
        return -1;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        err_set(err, -1, "could not open %s for writing", path);
        free(path);
        return -1;
    }

    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, fp);
    fclose(fp);
    free(path);

    if (written != len)
    {
        err_set(err, -1, "short write to cache file for appid %u/%s", appid, name);
        return -1;
    }

    return 0;
}

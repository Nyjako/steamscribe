#include "core/themes.h"

#include <cJSON.h>
#include <string.h>

#include "util/strbuf.h"

static const char *THEMES_SYSTEM_PROMPT =
    "You extract common themes from Steam player reviews so a human can pick which ones "
    "apply to their own experience. Follow these rules exactly:\n"
    "- Output JSON only, no prose, no markdown code fences.\n"
    "- Shape: {\"liked\": [{\"id\": \"...\", \"label\": \"...\"}], \"disliked\": [...]}\n"
    "- 5 to 8 items per side, most common first, no duplicates.\n"
    "- `id` is a short lowercase slug (letters, digits, underscores).\n"
    "- `label` is at most 4 words, written for a human to read in a checklist.\n"
    "- Base themes only on what reviewers actually say, never invent one.\n";

static void append_reviews(strbuf_t *sb, const review_set_t *reviews, bool voted_up)
{
    for (size_t i = 0; i < reviews->count; i++)
    {
        if (reviews->items[i].voted_up != voted_up) continue;
        strbuf_append_cstr(sb, "- ");
        strbuf_append_cstr(sb, reviews->items[i].text);
        strbuf_append_cstr(sb, "\n");
    }
}

void themes_build_prompt(arena_t *arena, const game_info_t *game, const review_set_t *reviews, char **out_system, char **out_user)
{
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_fmt(&sb, "Game: %s\n", game->name);
    strbuf_append_fmt(&sb, "Genres: %s\n", game->genres);
    strbuf_append_fmt(&sb, "Description: %s\n\n", game->short_desc);

    strbuf_append_cstr(&sb, "Positive reviews:\n");
    append_reviews(&sb, reviews, true);

    strbuf_append_cstr(&sb, "\nNegative reviews:\n");
    append_reviews(&sb, reviews, false);

    *out_system = arena_strdup(arena, THEMES_SYSTEM_PROMPT);
    *out_user = arena_strdup(arena, strbuf_cstr(&sb));

    strbuf_free(&sb);
}

/* Strips a leading/trailing ```[json] code fence if the LLM wrapped its
 * answer in one, leaving plain JSON text untouched. */
static char *strip_code_fences(arena_t *arena, const char *text)
{
    const char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\n' || start[len - 1] == '\r')) len--;

    if (len >= 3 && strncmp(start, "```", 3) == 0)
    {
        const char *nl = memchr(start, '\n', len);
        if (nl)
        {
            size_t skip = (size_t)(nl - start) + 1;
            start += skip;
            len -= skip;
        }

        if (len >= 3 && strncmp(start + len - 3, "```", 3) == 0)
        {
            len -= 3;
            while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r')) len--;
        }
    }

    return arena_strndup(arena, start, len);
}

static theme_t *parse_theme_array(arena_t *arena, cJSON *array, size_t *out_count)
{
    size_t count = cJSON_IsArray(array) ? (size_t)cJSON_GetArraySize(array) : 0;
    theme_t *items = count ? (theme_t *)arena_alloc(arena, count * sizeof(theme_t)) : NULL;

    for (size_t i = 0; i < count; i++)
    {
        cJSON *item = cJSON_GetArrayItem(array, (int)i);
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");

        items[i].id = cJSON_IsString(id) ? arena_strdup(arena, id->valuestring) : arena_strdup(arena, "");
        items[i].label = cJSON_IsString(label) ? arena_strdup(arena, label->valuestring) : arena_strdup(arena, "");
        items[i].checked = false;
    }

    *out_count = count;
    return items;
}

int themes_parse_response(arena_t *arena, const char *llm_text, theme_set_t *out, err_t *err)
{
    char *stripped = strip_code_fences(arena, llm_text);

    cJSON *root = cJSON_Parse(stripped);
    if (!root)
    {
        err_set(err, -1, "LLM response was not valid JSON");
        return -1;
    }

    cJSON *liked = cJSON_GetObjectItemCaseSensitive(root, "liked");
    cJSON *disliked = cJSON_GetObjectItemCaseSensitive(root, "disliked");

    if (!cJSON_IsArray(liked) || !cJSON_IsArray(disliked))
    {
        cJSON_Delete(root);
        err_set(err, -1, "LLM response is missing \"liked\"/\"disliked\" arrays");
        return -1;
    }

    out->liked = parse_theme_array(arena, liked, &out->n_liked);
    out->disliked = parse_theme_array(arena, disliked, &out->n_disliked);

    cJSON_Delete(root);
    return 0;
}

int themes_extract(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const review_set_t *reviews, theme_set_t *out, err_t *err)
{
    char *system = NULL;
    char *user = NULL;
    themes_build_prompt(arena, game, reviews, &system, &user);

    strbuf_t response;
    strbuf_init(&response);

    int rc = provider->complete(cfg, system, user, &response, err);
    if (rc == 0) rc = themes_parse_response(arena, strbuf_cstr(&response), out, err);

    if (rc != 0)
    {
        strbuf_t strict_system;
        strbuf_init(&strict_system);
        strbuf_append_cstr(&strict_system, system);
        strbuf_append_cstr(&strict_system, "\nYour previous response was not valid JSON. Return ONLY the JSON object, with no code fences and no other text.");

        rc = provider->complete(cfg, strbuf_cstr(&strict_system), user, &response, err);
        if (rc == 0) rc = themes_parse_response(arena, strbuf_cstr(&response), out, err);

        strbuf_free(&strict_system);
    }

    strbuf_free(&response);
    return rc;
}

char *themes_to_json(const theme_set_t *themes)
{
    cJSON *root = cJSON_CreateObject();

    cJSON *liked = cJSON_AddArrayToObject(root, "liked");
    for (size_t i = 0; i < themes->n_liked; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", themes->liked[i].id);
        cJSON_AddStringToObject(item, "label", themes->liked[i].label);
        cJSON_AddItemToArray(liked, item);
    }

    cJSON *disliked = cJSON_AddArrayToObject(root, "disliked");
    for (size_t i = 0; i < themes->n_disliked; i++)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", themes->disliked[i].id);
        cJSON_AddStringToObject(item, "label", themes->disliked[i].label);
        cJSON_AddItemToArray(disliked, item);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

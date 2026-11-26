#include "core/themes.h"

#include <cJSON.h>
#include <stdio.h>
#include <string.h>

#include "util/json_fence.h"
#include "util/strbuf.h"

/* Caps how many reviews go into a single side's prompt -- keeps prompts
 * small enough for small-context local models, and the LLM call itself
 * fast. Steam's `filter=all` sorts by helpfulness, so the first N of a
 * side are already the most representative ones. */
#define THEMES_MAX_REVIEWS_PER_SIDE 60

/* A conservative (safe-side) chars-per-token estimate for review text --
 * actual token density varies by content (BBCode/punctuation-heavy text
 * can run denser than plain prose), so this errs low rather than risk
 * still overflowing num_ctx. */
#define THEMES_CHARS_PER_TOKEN_ESTIMATE 3

/* Reserved for the system prompt, the JSON schema (when used), and the
 * model's own reply -- all of which share the same context window as the
 * reviews. */
#define THEMES_PROMPT_OVERHEAD_TOKENS 1500

/* How many characters of review text fit in num_ctx tokens' worth of
 * context, after THEMES_PROMPT_OVERHEAD_TOKENS. Returns 0 ("no limit",
 * keep the existing count-only cap) when num_ctx isn't set -- true for
 * every provider except ollama, where a game with unusually long reviews
 * can otherwise build a prompt that overflows the model's context and
 * gets rejected outright rather than just answered sloppily. */
static size_t themes_review_char_budget(int num_ctx)
{
    if (num_ctx <= 0) return 0;

    int available_tokens = num_ctx - THEMES_PROMPT_OVERHEAD_TOKENS;
    if (available_tokens < 500) available_tokens = 500;

    return (size_t)available_tokens * THEMES_CHARS_PER_TOKEN_ESTIMATE;
}

static const char *theme_side_key(bool is_liked_side)
{
    return is_liked_side ? "liked" : "disliked";
}

static const char *theme_side_system_prompt(bool is_liked_side)
{
    if (is_liked_side)
    {
        return "You extract the most common things players LIKED about a game from their "
               "Steam reviews, so a human can pick which ones match their own experience. "
               "Follow these rules exactly:\n"
               "- Output JSON only, no prose, no markdown code fences.\n"
               "- Shape: {\"liked\": [{\"id\": \"...\", \"label\": \"...\"}]}\n"
               "- 5 to 8 items, most common first, no duplicates.\n"
               "- `id` is a short lowercase slug (letters, digits, underscores).\n"
               "- `label` is at most 4 words, written for a human to read in a checklist.\n"
               "- Base items only on what reviewers actually say, never invent one.\n";
    }

    return "You extract the most common things players DISLIKED (complained about) in a "
           "game from their Steam reviews, so a human can pick which ones match their own "
           "experience. Follow these rules exactly:\n"
           "- Output JSON only, no prose, no markdown code fences.\n"
           "- Shape: {\"disliked\": [{\"id\": \"...\", \"label\": \"...\"}]}\n"
           "- 5 to 8 items, most common first, no duplicates.\n"
           "- `id` is a short lowercase slug (letters, digits, underscores).\n"
           "- `label` is at most 4 words, written for a human to read in a checklist.\n"
           "- Base items only on what reviewers actually say, never invent one.\n";
}

static const char *theme_side_json_schema(bool is_liked_side)
{
    if (is_liked_side)
    {
        return "{\"type\":\"object\",\"properties\":{\"liked\":{\"type\":\"array\",\"items\":"
               "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"label\":{\"type\":\"string\"}},"
               "\"required\":[\"id\",\"label\"]}}},\"required\":[\"liked\"]}";
    }

    return "{\"type\":\"object\",\"properties\":{\"disliked\":{\"type\":\"array\",\"items\":"
           "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"},\"label\":{\"type\":\"string\"}},"
           "\"required\":[\"id\",\"label\"]}}},\"required\":[\"disliked\"]}";
}

void themes_build_side_prompt(arena_t *arena, const game_info_t *game, const review_set_t *reviews, bool is_liked_side, int num_ctx, char **out_system, char **out_user)
{
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_fmt(&sb, "Game: %s\n", game->name);
    strbuf_append_fmt(&sb, "Genres: %s\n", game->genres);
    strbuf_append_fmt(&sb, "Description: %s\n\n", game->short_desc);

    strbuf_append_fmt(&sb, "%s reviews:\n", is_liked_side ? "Positive" : "Negative");

    size_t char_budget = themes_review_char_budget(num_ctx);

    size_t shown = 0;
    for (size_t i = 0; i < reviews->count && shown < THEMES_MAX_REVIEWS_PER_SIDE; i++)
    {
        if (reviews->items[i].voted_up != is_liked_side) continue;

        size_t review_len = strlen(reviews->items[i].text);
        if (char_budget > 0 && shown > 0 && sb.len + review_len > char_budget) break;

        strbuf_append_cstr(&sb, "- ");
        strbuf_append_cstr(&sb, reviews->items[i].text);
        strbuf_append_cstr(&sb, "\n");
        shown++;
    }

    if (shown == 0) strbuf_append_cstr(&sb, "(none)\n");

    *out_system = arena_strdup(arena, theme_side_system_prompt(is_liked_side));
    *out_user = arena_strdup(arena, strbuf_cstr(&sb));

    strbuf_free(&sb);
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

int themes_parse_side_response(arena_t *arena, const char *llm_text, bool is_liked_side, theme_t **out_items, size_t *out_count, err_t *err)
{
    char *stripped = json_strip_code_fences(arena, llm_text);

    cJSON *root = cJSON_Parse(stripped);
    if (!root)
    {
        err_set(err, -1, "LLM response was not valid JSON");
        return -1;
    }

    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, theme_side_key(is_liked_side));
    if (!cJSON_IsArray(array))
    {
        cJSON_Delete(root);
        err_set(err, -1, "LLM response is missing the \"%s\" array", theme_side_key(is_liked_side));
        return -1;
    }

    *out_items = parse_theme_array(arena, array, out_count);

    cJSON_Delete(root);
    return 0;
}

int themes_parse_response(arena_t *arena, const char *llm_text, theme_set_t *out, err_t *err)
{
    char *stripped = json_strip_code_fences(arena, llm_text);

    cJSON *root = cJSON_Parse(stripped);
    if (!root)
    {
        err_set(err, -1, "cached themes JSON was invalid");
        return -1;
    }

    cJSON *liked = cJSON_GetObjectItemCaseSensitive(root, "liked");
    cJSON *disliked = cJSON_GetObjectItemCaseSensitive(root, "disliked");

    if (!cJSON_IsArray(liked) || !cJSON_IsArray(disliked))
    {
        cJSON_Delete(root);
        err_set(err, -1, "cached themes JSON is missing \"liked\"/\"disliked\" arrays");
        return -1;
    }

    out->liked = parse_theme_array(arena, liked, &out->n_liked);
    out->disliked = parse_theme_array(arena, disliked, &out->n_disliked);

    cJSON_Delete(root);
    return 0;
}

static int themes_extract_side(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const review_set_t *reviews, bool is_liked_side, theme_t **out_items, size_t *out_count, err_t *err)
{
    char *system = NULL;
    char *user = NULL;
    themes_build_side_prompt(arena, game, reviews, is_liked_side, cfg->num_ctx, &system, &user);

    const char *schema = theme_side_json_schema(is_liked_side);

    strbuf_t response;
    strbuf_init(&response);

    int rc = provider->complete_json ? provider->complete_json(cfg, system, user, schema, &response, err) : provider->complete(cfg, system, user, &response, err);
    if (rc == 0) rc = themes_parse_side_response(arena, strbuf_cstr(&response), is_liked_side, out_items, out_count, err);

    if (rc != 0)
    {
        strbuf_t strict_system;
        strbuf_init(&strict_system);
        strbuf_append_cstr(&strict_system, system);
        strbuf_append_cstr(&strict_system, "\nYour previous response was not valid JSON. Return ONLY the JSON object, with no code fences and no other text.");

        const char *strict_system_cstr = strbuf_cstr(&strict_system);
        rc = provider->complete_json ? provider->complete_json(cfg, strict_system_cstr, user, schema, &response, err) : provider->complete(cfg, strict_system_cstr, user, &response, err);
        if (rc == 0) rc = themes_parse_side_response(arena, strbuf_cstr(&response), is_liked_side, out_items, out_count, err);

        strbuf_free(&strict_system);
    }

    strbuf_free(&response);
    return rc;
}

int themes_extract(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const review_set_t *reviews, theme_set_t *out, err_t *err)
{
    theme_t *liked = NULL;
    size_t n_liked = 0;
    if (themes_extract_side(arena, provider, cfg, game, reviews, true, &liked, &n_liked, err) != 0) return -1;

    theme_t *disliked = NULL;
    size_t n_disliked = 0;
    if (themes_extract_side(arena, provider, cfg, game, reviews, false, &disliked, &n_disliked, err) != 0) return -1;

    out->liked = liked;
    out->n_liked = n_liked;
    out->disliked = disliked;
    out->n_disliked = n_disliked;
    return 0;
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

void themes_add_custom_item(arena_t *arena, theme_t **items, size_t *count, const char *label)
{
    theme_t *grown = (theme_t *)arena_alloc(arena, (*count + 1) * sizeof(theme_t));
    if (*count > 0) memcpy(grown, *items, *count * sizeof(theme_t));

    char id[32];
    snprintf(id, sizeof(id), "custom_%zu", *count + 1);

    grown[*count].id = arena_strdup(arena, id);
    grown[*count].label = arena_strdup(arena, label);
    grown[*count].checked = true;

    *items = grown;
    (*count)++;
}

void themes_move_item(arena_t *arena, theme_t **from_items, size_t *from_count, theme_t **to_items, size_t *to_count, size_t index)
{
    if (index >= *from_count) return;

    theme_t moved = (*from_items)[index];
    for (size_t i = index; i + 1 < *from_count; i++) (*from_items)[i] = (*from_items)[i + 1];
    (*from_count)--;

    theme_t *grown = (theme_t *)arena_alloc(arena, (*to_count + 1) * sizeof(theme_t));
    if (*to_count > 0) memcpy(grown, *to_items, *to_count * sizeof(theme_t));
    grown[*to_count] = moved;

    *to_items = grown;
    (*to_count)++;
}

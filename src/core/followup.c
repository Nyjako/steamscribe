#include "core/followup.h"

#include <cJSON.h>
#include <stdbool.h>

#include "util/json_fence.h"
#include "util/strbuf.h"

static const char *FOLLOWUP_SYSTEM_PROMPT =
    "You help a player write a more personal Steam review by coming up with a few "
    "optional follow-up questions about their own experience with the game. Follow "
    "these rules exactly:\n"
    "- Output JSON only, no prose, no markdown code fences.\n"
    "- Shape: {\"questions\": [\"...\", \"...\"]}\n"
    "- 1 to 3 questions, each a single short sentence ending in a question mark.\n"
    "- Ask about specific, personal detail (a moment, a choice, a feeling) -- never a "
    "generic question like \"did you enjoy it?\" or anything a plain yes/no "
    "recommendation already covers.\n"
    "- Base questions on the game and the given liked/disliked points, not on generic "
    "genre trivia.\n";

static const char *FOLLOWUP_JSON_SCHEMA =
    "{\"type\":\"object\",\"properties\":{\"questions\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
    "\"required\":[\"questions\"]}";

void followup_build_prompt(arena_t *arena, const game_info_t *game, const theme_set_t *themes, char **out_system, char **out_user)
{
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_fmt(&sb, "Game: %s\n", game->name);
    strbuf_append_fmt(&sb, "Genres: %s\n\n", game->genres);

    strbuf_append_cstr(&sb, "What this player liked:\n");
    bool any_liked = false;
    for (size_t i = 0; i < themes->n_liked; i++)
    {
        if (!themes->liked[i].checked) continue;
        strbuf_append_fmt(&sb, "- %s\n", themes->liked[i].label);
        any_liked = true;
    }
    if (!any_liked) strbuf_append_cstr(&sb, "(nothing specific)\n");

    strbuf_append_cstr(&sb, "\nWhat bothered this player:\n");
    bool any_disliked = false;
    for (size_t i = 0; i < themes->n_disliked; i++)
    {
        if (!themes->disliked[i].checked) continue;
        strbuf_append_fmt(&sb, "- %s\n", themes->disliked[i].label);
        any_disliked = true;
    }
    if (!any_disliked) strbuf_append_cstr(&sb, "(nothing specific)\n");

    *out_system = arena_strdup(arena, FOLLOWUP_SYSTEM_PROMPT);
    *out_user = arena_strdup(arena, strbuf_cstr(&sb));

    strbuf_free(&sb);
}

int followup_parse_response(arena_t *arena, const char *llm_text, char ***out_questions, size_t *out_count, err_t *err)
{
    char *stripped = json_strip_code_fences(arena, llm_text);

    cJSON *root = cJSON_Parse(stripped);
    if (!root)
    {
        err_set(err, -1, "LLM response was not valid JSON");
        return -1;
    }

    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "questions");
    if (!cJSON_IsArray(array))
    {
        cJSON_Delete(root);
        err_set(err, -1, "LLM response is missing the \"questions\" array");
        return -1;
    }

    size_t count = (size_t)cJSON_GetArraySize(array);
    if (count > FOLLOWUP_MAX_QUESTIONS) count = FOLLOWUP_MAX_QUESTIONS;

    char **questions = count ? (char **)arena_alloc(arena, count * sizeof(char *)) : NULL;
    for (size_t i = 0; i < count; i++)
    {
        cJSON *item = cJSON_GetArrayItem(array, (int)i);
        questions[i] = cJSON_IsString(item) ? arena_strdup(arena, item->valuestring) : arena_strdup(arena, "");
    }

    *out_questions = questions;
    *out_count = count;

    cJSON_Delete(root);
    return 0;
}

int followup_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const game_info_t *game, const theme_set_t *themes, char ***out_questions, size_t *out_count, err_t *err)
{
    char *system = NULL;
    char *user = NULL;
    followup_build_prompt(arena, game, themes, &system, &user);

    strbuf_t response;
    strbuf_init(&response);

    int rc = provider->complete_json ? provider->complete_json(cfg, system, user, FOLLOWUP_JSON_SCHEMA, &response, err) : provider->complete(cfg, system, user, &response, err);
    if (rc == 0) rc = followup_parse_response(arena, strbuf_cstr(&response), out_questions, out_count, err);

    if (rc != 0)
    {
        strbuf_t strict_system;
        strbuf_init(&strict_system);
        strbuf_append_cstr(&strict_system, system);
        strbuf_append_cstr(&strict_system, "\nYour previous response was not valid JSON. Return ONLY the JSON object, with no code fences and no other text.");

        const char *strict_system_cstr = strbuf_cstr(&strict_system);
        rc = provider->complete_json ? provider->complete_json(cfg, strict_system_cstr, user, FOLLOWUP_JSON_SCHEMA, &response, err) : provider->complete(cfg, strict_system_cstr, user, &response, err);
        if (rc == 0) rc = followup_parse_response(arena, strbuf_cstr(&response), out_questions, out_count, err);

        strbuf_free(&strict_system);
    }

    strbuf_free(&response);
    return rc;
}

#include "core/spoiler_predict.h"

#include <cJSON.h>
#include <stdbool.h>

#include "util/json_fence.h"
#include "util/strbuf.h"

static const char *SPOILER_PREDICT_SYSTEM_PROMPT =
    "You help a player flag which part, if any, of their own notes about a game sounds "
    "like a plot spoiler (an ending, a major death, a twist, a reveal), so they can "
    "double check it before it's shown separately from the rest of their review. Follow "
    "these rules exactly:\n"
    "- Output JSON only, no prose, no markdown code fences.\n"
    "- Shape: {\"spoiler\": \"...\"}\n"
    "- Quote verbatim from the player's own notes/answers below -- never invent, "
    "paraphrase, or add anything that isn't already there.\n"
    "- Only flag genuine plot/story spoilers (what happens, who dies, how it ends, a "
    "twist) -- never a plain opinion about gameplay, difficulty, or performance.\n"
    "- If nothing below sounds like a spoiler, return {\"spoiler\": \"\"}. This is the "
    "common case -- don't force a match that isn't really there.\n";

static const char *SPOILER_PREDICT_JSON_SCHEMA = "{\"type\":\"object\",\"properties\":{\"spoiler\":{\"type\":\"string\"}},\"required\":[\"spoiler\"]}";

void spoiler_predict_build_prompt(arena_t *arena, const char *game_name, const char *notes, const char *const *answers, size_t n_answers, char **out_system, char **out_user)
{
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_fmt(&sb, "Game: %s\n\n", game_name);
    strbuf_append_fmt(&sb, "Notes:\n%s\n", (notes && notes[0]) ? notes : "(none)");

    bool any_answer = false;
    for (size_t i = 0; i < n_answers; i++)
    {
        if (!answers[i] || !answers[i][0]) continue;
        if (!any_answer)
        {
            strbuf_append_cstr(&sb, "\nMore about their experience:\n");
            any_answer = true;
        }
        strbuf_append_fmt(&sb, "- %s\n", answers[i]);
    }

    *out_system = arena_strdup(arena, SPOILER_PREDICT_SYSTEM_PROMPT);
    *out_user = arena_strdup(arena, strbuf_cstr(&sb));

    strbuf_free(&sb);
}

int spoiler_predict_parse_response(arena_t *arena, const char *llm_text, char **out_spoiler, err_t *err)
{
    char *stripped = json_strip_code_fences(arena, llm_text);

    cJSON *root = cJSON_Parse(stripped);
    if (!root)
    {
        err_set(err, -1, "LLM response was not valid JSON");
        return -1;
    }

    cJSON *spoiler = cJSON_GetObjectItemCaseSensitive(root, "spoiler");
    if (!cJSON_IsString(spoiler))
    {
        cJSON_Delete(root);
        err_set(err, -1, "LLM response is missing the \"spoiler\" string");
        return -1;
    }

    *out_spoiler = arena_strdup(arena, spoiler->valuestring);

    cJSON_Delete(root);
    return 0;
}

int spoiler_predict_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const char *game_name, const char *notes, const char *const *answers, size_t n_answers, char **out_spoiler, err_t *err)
{
    char *system = NULL;
    char *user = NULL;
    spoiler_predict_build_prompt(arena, game_name, notes, answers, n_answers, &system, &user);

    strbuf_t response;
    strbuf_init(&response);

    int rc = provider->complete_json ? provider->complete_json(cfg, system, user, SPOILER_PREDICT_JSON_SCHEMA, &response, err) : provider->complete(cfg, system, user, &response, err);
    if (rc == 0) rc = spoiler_predict_parse_response(arena, strbuf_cstr(&response), out_spoiler, err);

    if (rc != 0)
    {
        strbuf_t strict_system;
        strbuf_init(&strict_system);
        strbuf_append_cstr(&strict_system, system);
        strbuf_append_cstr(&strict_system, "\nYour previous response was not valid JSON. Return ONLY the JSON object, with no code fences and no other text.");

        const char *strict_system_cstr = strbuf_cstr(&strict_system);
        rc = provider->complete_json ? provider->complete_json(cfg, strict_system_cstr, user, SPOILER_PREDICT_JSON_SCHEMA, &response, err) : provider->complete(cfg, strict_system_cstr, user, &response, err);
        if (rc == 0) rc = spoiler_predict_parse_response(arena, strbuf_cstr(&response), out_spoiler, err);

        strbuf_free(&strict_system);
    }

    strbuf_free(&response);
    return rc;
}

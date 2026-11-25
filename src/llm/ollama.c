#include "llm/ollama.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>

#include "net/http.h"

#define OLLAMA_DEFAULT_BASE_URL "http://localhost:11434"

/* Local CPU inference can take well past libcurl's default 30s timeout. */
#define OLLAMA_TOTAL_TIMEOUT_S 180

static int ollama_chat(const llm_cfg_t *cfg, const char *system, const char *user, const char *json_schema, strbuf_t *out, err_t *err)
{
    strbuf_clear(out);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON_AddFalseToObject(root, "stream");
    cJSON_AddFalseToObject(root, "think");

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system);
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user);
    cJSON_AddItemToArray(messages, user_msg);

    if (json_schema)
    {
        cJSON *schema = cJSON_Parse(json_schema);
        if (schema) cJSON_AddItemToObject(root, "format", schema);
    }

    if (cfg->num_ctx > 0)
    {
        cJSON *options = cJSON_AddObjectToObject(root, "options");
        cJSON_AddNumberToObject(options, "num_ctx", cfg->num_ctx);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
    {
        err_set(err, -1, "out of memory building ollama request");
        return -1;
    }

    const char *base_url = (cfg->base_url && cfg->base_url[0]) ? cfg->base_url : OLLAMA_DEFAULT_BASE_URL;
    char url[512];
    snprintf(url, sizeof(url), "%s/api/chat", base_url);

    http_cfg_t http_cfg = http_cfg_default();
    http_cfg.total_timeout_s = OLLAMA_TOTAL_TIMEOUT_S;

    strbuf_t response;
    strbuf_init(&response);

    int rc = http_post_json(&http_cfg, url, NULL, 0, body, &response, err);
    free(body);

    if (rc != 0)
    {
        strbuf_free(&response);
        return rc;
    }

    cJSON *resp_root = cJSON_Parse(strbuf_cstr(&response));
    strbuf_free(&response);
    if (!resp_root)
    {
        err_set(err, -1, "invalid JSON from Ollama");
        return -1;
    }

    cJSON *message = cJSON_GetObjectItemCaseSensitive(resp_root, "message");
    cJSON *content = message ? cJSON_GetObjectItemCaseSensitive(message, "content") : NULL;

    if (!cJSON_IsString(content))
    {
        cJSON *error_field = cJSON_GetObjectItemCaseSensitive(resp_root, "error");
        err_set(err, -1, "Ollama response had no content: %s", cJSON_IsString(error_field) ? error_field->valuestring : "unknown error");
        cJSON_Delete(resp_root);
        return -1;
    }

    strbuf_append_cstr(out, content->valuestring);
    cJSON_Delete(resp_root);
    return 0;
}

static int ollama_complete(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err)
{
    return ollama_chat(cfg, system, user, NULL, out, err);
}

static int ollama_complete_json(const llm_cfg_t *cfg, const char *system, const char *user, const char *json_schema, strbuf_t *out, err_t *err)
{
    return ollama_chat(cfg, system, user, json_schema, out, err);
}

const llm_provider_t llm_provider_ollama = {"ollama", ollama_complete, ollama_complete_json};

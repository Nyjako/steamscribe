#include "llm/openai.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>

#include "net/http.h"

#define OPENAI_DEFAULT_BASE_URL "https://api.openai.com/v1"

/* This provider also covers local servers (LM Studio, llama.cpp's
 * llama-server, ...) via base_url, and local CPU inference can take well
 * past libcurl's default 30s timeout -- same reasoning as llm/ollama.c. */
#define OPENAI_TOTAL_TIMEOUT_S 180

static int openai_complete(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err)
{
    strbuf_clear(out);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system);
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user);
    cJSON_AddItemToArray(messages, user_msg);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
    {
        err_set(err, -1, "out of memory building openai request");
        return -1;
    }

    const char *base_url = (cfg->base_url && cfg->base_url[0]) ? cfg->base_url : OPENAI_DEFAULT_BASE_URL;
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", base_url);

    http_header_t headers[1];
    size_t header_count = 0;
    char auth_value[512];
    if (cfg->api_key && cfg->api_key[0])
    {
        snprintf(auth_value, sizeof(auth_value), "Bearer %s", cfg->api_key);
        headers[0].name = "Authorization";
        headers[0].value = auth_value;
        header_count = 1;
    }

    http_cfg_t http_cfg = http_cfg_default();
    http_cfg.total_timeout_s = OPENAI_TOTAL_TIMEOUT_S;

    strbuf_t response;
    strbuf_init(&response);

    int rc = http_post_json(&http_cfg, url, headers, header_count, body, &response, err);
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
        err_set(err, -1, "invalid JSON from %s", url);
        return -1;
    }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(resp_root, "choices");
    cJSON *first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *message = first ? cJSON_GetObjectItemCaseSensitive(first, "message") : NULL;
    cJSON *content = message ? cJSON_GetObjectItemCaseSensitive(message, "content") : NULL;

    if (!cJSON_IsString(content))
    {
        cJSON *error_obj = cJSON_GetObjectItemCaseSensitive(resp_root, "error");
        cJSON *error_msg = error_obj ? cJSON_GetObjectItemCaseSensitive(error_obj, "message") : NULL;
        err_set(err, -1, "%s response had no content: %s", url, cJSON_IsString(error_msg) ? error_msg->valuestring : "unknown error");
        cJSON_Delete(resp_root);
        return -1;
    }

    strbuf_append_cstr(out, content->valuestring);
    cJSON_Delete(resp_root);
    return 0;
}

const llm_provider_t llm_provider_openai = {"openai", openai_complete, NULL, NULL};

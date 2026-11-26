#include "llm/anthropic.h"

#include <cJSON.h>
#include <stdlib.h>

#include "net/http.h"

#define ANTHROPIC_API_VERSION "2023-06-01"
#define ANTHROPIC_MAX_TOKENS 1024

static int anthropic_complete(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err)
{
    strbuf_clear(out);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON_AddNumberToObject(root, "max_tokens", ANTHROPIC_MAX_TOKENS);
    cJSON_AddStringToObject(root, "system", system);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", user);
    cJSON_AddItemToArray(messages, message);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
    {
        err_set(err, -1, "out of memory building anthropic request");
        return -1;
    }

    http_header_t headers[2];
    headers[0].name = "x-api-key";
    headers[0].value = cfg->api_key;
    headers[1].name = "anthropic-version";
    headers[1].value = ANTHROPIC_API_VERSION;

    http_cfg_t http_cfg = http_cfg_default();
    strbuf_t response;
    strbuf_init(&response);

    int rc = http_post_json(&http_cfg, "https://api.anthropic.com/v1/messages", headers, 2, body, &response, err);
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
        err_set(err, -1, "invalid JSON from Anthropic");
        return -1;
    }

    cJSON *content = cJSON_GetObjectItemCaseSensitive(resp_root, "content");
    cJSON *first = cJSON_IsArray(content) ? cJSON_GetArrayItem(content, 0) : NULL;
    cJSON *text = first ? cJSON_GetObjectItemCaseSensitive(first, "text") : NULL;

    if (!cJSON_IsString(text))
    {
        cJSON *error_obj = cJSON_GetObjectItemCaseSensitive(resp_root, "error");
        cJSON *error_msg = error_obj ? cJSON_GetObjectItemCaseSensitive(error_obj, "message") : NULL;
        err_set(err, -1, "Anthropic response had no text: %s", cJSON_IsString(error_msg) ? error_msg->valuestring : "unknown error");
        cJSON_Delete(resp_root);
        return -1;
    }

    strbuf_append_cstr(out, text->valuestring);
    cJSON_Delete(resp_root);
    return 0;
}

const llm_provider_t llm_provider_anthropic = {"anthropic", anthropic_complete, NULL};

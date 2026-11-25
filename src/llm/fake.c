#include "llm/fake.h"

#include <stdlib.h>
#include <string.h>

static char *fake_first_response = NULL;
static char *fake_response = NULL;
static int fake_call_count = 0;

void llm_fake_set_response(const char *text)
{
    free(fake_first_response);
    fake_first_response = NULL;
    free(fake_response);
    fake_response = text ? strdup(text) : NULL;
    fake_call_count = 0;
}

void llm_fake_set_sequence(const char *first, const char *rest)
{
    free(fake_first_response);
    fake_first_response = first ? strdup(first) : NULL;
    free(fake_response);
    fake_response = rest ? strdup(rest) : NULL;
    fake_call_count = 0;
}

static int fake_complete(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err)
{
    (void)cfg;
    (void)system;
    (void)user;

    strbuf_clear(out);

    const char *text = (fake_call_count == 0 && fake_first_response) ? fake_first_response : fake_response;
    fake_call_count++;

    if (!text)
    {
        err_set(err, -1, "fake provider has no response set");
        return -1;
    }

    if (!strbuf_append_cstr(out, text))
    {
        err_set(err, -1, "out of memory");
        return -1;
    }

    return 0;
}

const llm_provider_t llm_provider_fake = {"fake", fake_complete, NULL};

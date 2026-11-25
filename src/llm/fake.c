#include "llm/fake.h"

#include <stdlib.h>
#include <string.h>

static char *fake_response = NULL;

void llm_fake_set_response(const char *text)
{
    free(fake_response);
    fake_response = text ? strdup(text) : NULL;
}

static int fake_complete(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err)
{
    (void)cfg;
    (void)system;
    (void)user;

    strbuf_clear(out);

    if (!fake_response)
    {
        err_set(err, -1, "fake provider has no response set");
        return -1;
    }

    if (!strbuf_append_cstr(out, fake_response))
    {
        err_set(err, -1, "out of memory");
        return -1;
    }

    return 0;
}

const llm_provider_t llm_provider_fake = {"fake", fake_complete};

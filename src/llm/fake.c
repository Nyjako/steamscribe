#include "llm/fake.h"

#include <stdlib.h>
#include <string.h>

static char **fake_responses = NULL;
static size_t fake_response_count = 0;
static int fake_call_count = 0;

static void free_responses(void)
{
    for (size_t i = 0; i < fake_response_count; i++) free(fake_responses[i]);
    free(fake_responses);
    fake_responses = NULL;
    fake_response_count = 0;
}

void llm_fake_set_response_sequence(const char *const *responses, size_t count)
{
    free_responses();

    if (count > 0)
    {
        fake_responses = (char **)malloc(count * sizeof(char *));
        for (size_t i = 0; i < count; i++) fake_responses[i] = responses[i] ? strdup(responses[i]) : NULL;
        fake_response_count = count;
    }

    fake_call_count = 0;
}

void llm_fake_set_response(const char *text)
{
    llm_fake_set_response_sequence(&text, text ? 1 : 0);
}

void llm_fake_set_sequence(const char *first, const char *rest)
{
    const char *seq[2] = {first, rest};
    llm_fake_set_response_sequence(seq, 2);
}

static int fake_complete(const llm_cfg_t *cfg, const char *system, const char *user, strbuf_t *out, err_t *err)
{
    (void)cfg;
    (void)system;
    (void)user;

    strbuf_clear(out);

    const char *text = NULL;
    if (fake_response_count > 0)
    {
        size_t idx = (size_t)fake_call_count < fake_response_count ? (size_t)fake_call_count : fake_response_count - 1;
        text = fake_responses[idx];
    }
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

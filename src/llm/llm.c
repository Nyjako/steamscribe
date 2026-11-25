#include "llm/llm.h"

#include <string.h>

#include "llm/anthropic.h"
#include "llm/fake.h"
#include "llm/ollama.h"
#include "llm/openai.h"

const llm_provider_t *llm_provider_find(const char *name)
{
    if (!name) return NULL;

    if (strcmp(name, llm_provider_anthropic.name) == 0) return &llm_provider_anthropic;
    if (strcmp(name, llm_provider_openai.name) == 0) return &llm_provider_openai;
    if (strcmp(name, llm_provider_ollama.name) == 0) return &llm_provider_ollama;
    if (strcmp(name, llm_provider_fake.name) == 0) return &llm_provider_fake;

    return NULL;
}

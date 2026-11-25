#ifndef STEAMSCRIBE_LLM_FAKE_H
#define STEAMSCRIBE_LLM_FAKE_H

#include "llm/llm.h"

extern const llm_provider_t llm_provider_fake;

/* Sets the canned text llm_provider_fake.complete() returns on every call
 * until changed again. Test-only. */
void llm_fake_set_response(const char *text);

#endif

#ifndef STEAMSCRIBE_LLM_FAKE_H
#define STEAMSCRIBE_LLM_FAKE_H

#include "llm/llm.h"

extern const llm_provider_t llm_provider_fake;

/* Sets the canned text llm_provider_fake.complete() returns on every call
 * until changed again. Test-only. */
void llm_fake_set_response(const char *text);

/* Like llm_fake_set_response, but the very first call returns `first` and
 * every call after that returns `rest` -- useful for exercising a caller's
 * retry-once-on-failure path. Test-only. */
void llm_fake_set_sequence(const char *first, const char *rest);

#endif

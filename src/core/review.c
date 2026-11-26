#include "core/review.h"

#include "util/strbuf.h"

static const char *length_instruction(review_len_t length)
{
    switch (length)
    {
        case REVIEW_LEN_MEDIUM: return "1 to 2 paragraphs";
        case REVIEW_LEN_LONG: return "3 to 4 paragraphs";
        case REVIEW_LEN_SHORT:
        default: return "2 to 4 sentences";
    }
}

static const char *tone_instruction(review_tone_t tone)
{
    switch (tone)
    {
        case REVIEW_TONE_NEUTRAL: return "Write in a neutral, matter-of-fact tone.";
        case REVIEW_TONE_ENTHUSIASTIC: return "Write with enthusiasm and energy; the occasional emoji is fine.";
        case REVIEW_TONE_CASUAL:
        default: return "Write casually, like you're telling a friend about it.";
    }
}

/* Empty for the normal case so the golden prompt text doesn't grow a line
 * nobody asked for -- only REVIEW_VOCAB_SIMPLE adds an instruction. */
static const char *vocab_instruction(review_vocab_t vocab)
{
    switch (vocab)
    {
        case REVIEW_VOCAB_SIMPLE: return "Use simple, everyday words -- avoid fancy or technical vocabulary.";
        case REVIEW_VOCAB_NORMAL:
        default: return "";
    }
}

void review_build_prompt(arena_t *arena, const review_request_t *req, char **out_system, char **out_user)
{
    strbuf_t system;
    strbuf_init(&system);

    strbuf_append_cstr(&system, "You write a short, honest Steam review in the voice of the player themselves, using only the points they give you. Follow these rules exactly:\n");
    strbuf_append_cstr(&system, "- Write in first person, as the player.\n");
    strbuf_append_cstr(&system, "- Use only the provided points below. Never invent or add any extra like, dislike, experience, number, or feature -- not even ones that sound typical for this kind of game.\n");
    strbuf_append_cstr(&system, "- Plain text only: no headings, no markdown, no emojis unless told otherwise below.\n");
    strbuf_append_cstr(&system, "- End with a single-line verdict stating whether you recommend the game.\n");
    if (req->rating > 0) strbuf_append_fmt(&system, "- The player also gave it a rating. Work \"(%d/10)\" into that verdict line exactly as written -- don't change the number.\n", req->rating);
    strbuf_append_fmt(&system, "- Length: %s.\n", length_instruction(req->length));
    strbuf_append_fmt(&system, "- %s\n", tone_instruction(req->tone));

    const char *vocab_line = vocab_instruction(req->vocab);
    if (vocab_line[0]) strbuf_append_fmt(&system, "- %s\n", vocab_line);

    strbuf_t user;
    strbuf_init(&user);

    strbuf_append_fmt(&user, "Game: %s\n", req->game->name);

    strbuf_append_cstr(&user, "\nWhat I liked:\n");
    bool any_liked = false;
    for (size_t i = 0; i < req->themes->n_liked; i++)
    {
        if (!req->themes->liked[i].checked) continue;
        strbuf_append_fmt(&user, "- %s\n", req->themes->liked[i].label);
        any_liked = true;
    }
    if (!any_liked) strbuf_append_cstr(&user, "(nothing specific)\n");

    strbuf_append_cstr(&user, "\nWhat bothered me:\n");
    bool any_disliked = false;
    for (size_t i = 0; i < req->themes->n_disliked; i++)
    {
        if (!req->themes->disliked[i].checked) continue;
        strbuf_append_fmt(&user, "- %s\n", req->themes->disliked[i].label);
        any_disliked = true;
    }
    if (!any_disliked) strbuf_append_cstr(&user, "(nothing specific)\n");

    bool any_followup = false;
    for (size_t i = 0; i < req->n_followup; i++)
    {
        if (!req->followup_answers[i] || !req->followup_answers[i][0]) continue;
        if (!any_followup)
        {
            strbuf_append_cstr(&user, "\nMore about my experience:\n");
            any_followup = true;
        }
        strbuf_append_fmt(&user, "- %s %s\n", req->followup_questions[i], req->followup_answers[i]);
    }

    strbuf_append_fmt(&user, "\nAdditional notes: %s\n", (req->notes && req->notes[0]) ? req->notes : "(none)");
    strbuf_append_fmt(&user, "\nOverall recommendation: %s\n", req->recommend ? "yes" : "no");
    if (req->rating > 0) strbuf_append_fmt(&user, "Rating: %d/10\n", req->rating);

    *out_system = arena_strdup(arena, strbuf_cstr(&system));
    *out_user = arena_strdup(arena, strbuf_cstr(&user));

    strbuf_free(&system);
    strbuf_free(&user);
}

char *review_append_spoiler(arena_t *arena, const review_request_t *req, const char *generated_text)
{
    if (!req->spoiler_notes || !req->spoiler_notes[0]) return arena_strdup(arena, generated_text);

    strbuf_t out;
    strbuf_init(&out);
    strbuf_append_cstr(&out, generated_text);

    if (req->bbcode) strbuf_append_fmt(&out, "\n\n[spoiler]%s[/spoiler]", req->spoiler_notes);
    else strbuf_append_fmt(&out, "\n\nSpoilers: %s", req->spoiler_notes);

    char *result = arena_strdup(arena, strbuf_cstr(&out));
    strbuf_free(&out);
    return result;
}

int review_generate(arena_t *arena, const llm_provider_t *provider, const llm_cfg_t *cfg, const review_request_t *req, char **out_text, err_t *err)
{
    char *system = NULL;
    char *user = NULL;
    review_build_prompt(arena, req, &system, &user);

    strbuf_t response;
    strbuf_init(&response);

    int rc = provider->complete(cfg, system, user, &response, err);
    if (rc == 0) *out_text = review_append_spoiler(arena, req, strbuf_cstr(&response));

    strbuf_free(&response);
    return rc;
}

# steamscribe – Design Document

## 1. Goal

Help a player write a short, honest Steam review in under two minutes, without leaving the terminal.

**In scope**
- Find a game, read public Steam data, extract common themes, ask the user, generate text.

**Out of scope (for now)**
- Posting reviews to Steam (the user copies and pastes)
- Steam login or account access
- Windows native support (WSL works)
- GUI or browser extension

**Principles**
1. **The user's opinion only.** Themes from other reviews are *questions*, never content.
2. **Short and plain by default.** Natural writing beats polished AI prose.
3. **Be nice to Steam.** Cache aggressively and keep requests few and sequential.
4. **Small and dependency-light.** One static-ish binary and few libraries.

---

## 2. User flow

```
 ┌──────────┐   ┌──────────┐   ┌───────────────┐   ┌────────────┐
 │  Search  │──▶│  Select  │──▶│ Fetch+Analyze │──▶│   Liked    │
 └──────────┘   └──────────┘   │   (spinner)   │   │ checklist  │
                               └───────────────┘   └─────┬──────┘
 ┌──────────┐   ┌──────────┐   ┌───────────────┐   ┌─────▼──────┐
 │  Result  │◀──│ Generate │◀──│ Notes + y/n + │◀──│  Disliked  │
 │ c/e/r/s/q│   │(spinner) │   │ length/tone   │   │ checklist  │
 └──────────┘   └──────────┘   └───────────────┘   └────────────┘
```

The TUI is a simple **state machine**. Each screen is a state with `draw()` and `handle_event()`, and transitions go forward or back (Esc).

---

## 3. External data

All endpoints are public JSON and need no key, except 3.5 (playtime), which is opt-in.

### 3.1 Search
`GET https://store.steampowered.com/api/storesearch/?term=<q>&cc=us&l=english`
→ `items[]: { id, name, type, ... }`

### 3.2 Game details
`GET https://store.steampowered.com/api/appdetails?appids=<id>&l=english`
→ `{ "<id>": { success, data: { name, short_description, genres[], categories[], developers[], release_date, ... } } }`

(User tags aren't included. Genres and description are enough context.)

### 3.3 Reviews
`GET https://api.steampowered.com/IUserReviewsService/GetAppReviews/v1/?input_json=<json>`

The old `store.steampowered.com/appreviews/<id>?json=1` endpoint is deprecated and
redirects every plain HTTP client (confirmed from multiple networks -- looked like a
TLS/fingerprint-based bot check). `IUserReviewsService.GetAppReviews` is Valve's
documented replacement and works fine keyless. It's a Service-interface method, so
every parameter besides the optional `key` travels inside a single JSON-encoded
`input_json` query value rather than as flat query params:
`{"appid": <id>, "filter": 1, "review_type": <0|1|2>, "purchase_type": 1, "num_per_page": 100, "cursor": "<c>", "languages": ["english"]}`

- `filter` is an int enum (`IUserReviewsService#EUserReviewsAppReviewsFilter`): we use
  `1` (Recent), not the default `0` (Helpful) -- Helpful widens its day_range until it
  finds results and never runs dry, so a cursor loop relying on an eventual empty page
  to terminate would loop forever.
- `review_type` (`0` all, `1` positive, `2` negative) and `purchase_type` (`1` = all,
  matching the old `purchase_type=all`) are int enums too.
- `cursor` starts as `*`, comes back in each response, and **must be URL-encoded** on
  the next request (it's one value inside `input_json`, which itself is URL-encoded as
  a whole).
- The response has no `success` field -- check the HTTP status instead. Everything is
  nested under a top-level `response` object, which is `{}` for an unknown appid.
- `query_summary` is present on every page, but its score fields (`review_score_desc`,
  `total_positive`, `total_negative`) only appear on the first page of a
  `review_type=0` (all) query.
- Each review has `review` (text), `voted_up`, `votes_up`, `author.playtime_forever`,
  and more.

**Strategy:** fetch N/2 positive and N/2 negative (default 100 + 100). Drop very short reviews (< 40 chars) and clip each to about 1,200 chars before sending to the LLM.

### 3.4 Request etiquette
- User-Agent: `steamscribe/<version>`
- Sequential requests with a ~300 ms delay between review pages
- 10 s connect and 30 s total timeout, with 2 retries and backoff on 429/5xx

### 3.5 Playtime (optional)
`GET https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/?key=<key>&steamid=<id>&appids_filter[0]=<appid>&include_appinfo=false&include_played_free_games=true&format=json`
→ `{"response": {"games": [{"appid": ..., "playtime_forever": <minutes>, ...}]}}` -- `games`
is absent (or empty) if the account doesn't own that appid, or the profile/game details
are private. This is the **Steam Web API**, a different service from 3.1-3.3 above (which
are the public store/community endpoints): it needs the account's own free API key and
SteamID64 (`[steam] steam_id` in config.ini -- not a secret, so it's fine to keep there
as plain text). The key itself can come from `STEAM_API_KEY` (env, same as the LLM keys,
and takes priority if set), or be pasted straight into `[steam] steam_api_key` in
config.ini -- see 7.1 for how that's kept off disk as plain text. Authenticates via a
`key=` query parameter rather than a header, which is why
`pipeline_fetch_playtime()` deliberately does *not* log `err.msg` on failure (it's built
from the full request URL, which would leak the key into `debug.log`) -- it logs just the
HTTP status code instead.

Entirely best-effort, same pattern as the follow-up questions: a missing key/id, or any
failure calling the API, just means no playtime is passed to the review prompt, never a
hard error. Fetched lazily right before generating the review (not during the game-load
fetch, since it's cheap and only needed at that point), and converted from minutes to
whole hours, so playtime under 60 minutes just doesn't surface at all.

---

## 4. LLM usage

Up to four calls per review: two to extract themes (liked/disliked), one optional one for
a few follow-up questions, and one to generate the review text.

### 4.1 Theme extraction
Two calls, not one: liked themes are extracted from only the positive reviews, disliked
themes from only the negative ones, then merged. This lets a schema-capable provider
constrain each call to a simple, single-key shape, and keeps each side's prompt no
larger than it needs to be for that side alone.

How many reviews make it into a side's prompt is governed purely by a character budget,
not a fixed count -- so `review_count` in Settings (how many reviews
`steam_reviews_sample` fetches in the first place) is what actually controls how much
gets analyzed. For `ollama`, that budget is computed from `num_ctx` -- a conservative
chars-per-token estimate, less `THEMES_PROMPT_OVERHEAD_TOKENS` reserved for the system
prompt, the schema, and the model's own reply. Without this, a game whose reviews happen
to be unusually long (e.g. long BBCode-formatted "Pros/Cons" lists) could build a prompt
that overflows the model's context outright -- Ollama rejects that with an HTTP 400
("request exceeds the available context size") rather than just answering sloppily,
which surfaced as "Could not load game" with no further explanation. Other providers
don't set `num_ctx`, so they fall back to a generous fixed budget
(`THEMES_DEFAULT_CHAR_BUDGET`) instead -- large enough that their big hosted context
windows are never the limiting factor, but still a hard ceiling so a high `review_count`
can't build an unbounded, runaway-cost prompt.

```json
{ "liked":    [ { "id": "art",      "label": "Art style & lighting" } ] }
{ "disliked": [ { "id": "raids",    "label": "Annoying enemy raids" } ] }
```

Rules in prompt: at least 7 items, up to 14, short labels (≤ 4 words), most common first,
no duplicates, JSON only -- the schema itself also sets `minItems`/`maxItems` (7/14) for a
schema-capable provider, since a small local model given only a loose "N to M" range in
the prompt text tended to always settle on the low end (consistently exactly 7) rather
than actually varying with how many distinct points the reviews supported.

Parsing: strip ```` ``` ```` fences if present and parse with cJSON. On failure, retry
that side once with "return only valid JSON". On a second failure, show an error with the
raw output in a debug log.

The user can also add their own item to either side's checklist -- a label the LLM never
surfaced, typed in and checked by default (`themes_add_custom_item`, in both the TUI and
`--plain`). These are appended to the in-memory list used for that one review; they are
never written back into the themes cache, so a fresh load for the same game won't carry
someone else's custom entry.

Either side's items can also be moved to the other side (`themes_move_item`), id/label/
checked state carried over unchanged -- for when the AI filed something under the wrong
one of this particular player's own opinions (e.g. a commonly-complained-about mechanic
that this player actually enjoys). In the TUI that's `m` on the Liked/Disliked screens;
`--plain` asks "Any of those actually belong under ... instead?" right after the
add-your-own prompt, against the originally-printed numbering (adding custom items first
doesn't shift it).

### 4.2 Follow-up questions
Right after Disliked, one more call (`core/followup.c`, same two-pass-extraction style as
4.1: JSON-only, a schema when the provider supports it, one retry on invalid JSON) asks
for 1–3 short, specific, optional questions about *this* player's experience, built from
only their *checked* liked/disliked labels -- e.g. "Which structure did you build that
made the resource gathering feel worth it?" rather than "did you enjoy it?". Capped to
`FOLLOWUP_MAX_QUESTIONS` (3), which also sizes the TUI's fixed per-question answer
buffers.

This step is best-effort and never blocks the flow: a failure (bad JSON after the retry,
a network error, anything) is treated as zero questions and skips straight to the
free-text "Anything else?" step, with no error shown -- it was never required to begin
with. Each question can be answered or left blank (skipped); only answered ones make it
into the review prompt (see 4.3), as a `- <question> <answer>` line each, under a "More
about my experience:" heading that's left out entirely if nothing was answered.

### 4.3 Review generation
Input: game name, **only the checked labels**, any answered follow-up questions, the user's free-text notes, recommend yes/no, an optional 1-10 rating, an optional playtime (see 3.5), length, tone, vocabulary.
System prompt rules:
- Write in first person as a regular player.
- Use only the provided points below. Never invent or add any extra like, dislike,
  experience, number, or feature -- not even ones that sound typical for this kind of
  game (worded this strongly because a local model otherwise did exactly that: added an
  unchecked, genre-typical complaint on its own).
- Playtime: when available (`playtime_hours > 0`), one extra rule tells the model it *may*
  work it in naturally (e.g. "After 120 hours...") but never force it in and never mention
  it twice -- unlike the no-invented-content rule above, this one's explicitly optional,
  since forcing every single review to open with a playtime callout would get repetitive
  fast.
- Short = 2–4 sentences. Medium = 1–2 paragraphs. Long = 3–4 paragraphs.
- Plain text, no headings, no emojis unless the tone is "enthusiastic". End with a one-line verdict.
- Rating: a new optional step right after Recommend, in both the TUI and `--plain` --
  blank/Enter skips it (same "Enter skips" convention used elsewhere), otherwise 1-10,
  clamped rather than rejected if someone types e.g. "99". When given, one extra system
  rule tells the model to work "(N/10)" into the closing verdict line exactly as given --
  phrased as strongly as the "never invent a number" rule above, for the same reason: it's
  still a number in the prompt, and the model shouldn't feel free to round it or restate
  it differently.
- Vocabulary: "normal" adds no extra line; "simple" adds one telling the model to use
  plain, everyday words and avoid fancy or technical vocabulary. Picked per review, right
  after length, in both the TUI and `--plain`; defaults from `[review] default_vocab`.

**Format and spoilers.** Right after Vocab, a "Format: (p)lain / (b)bcode" step, then an
always-asked, optional "Spoiler details" step -- a separate text field, not folded into
the main notes. Spoiler text is deliberately never sent to the model at all: it's
appended to the generated text as-is, after generation (`review_append_spoiler()`, pure
and unit-tested without a network dependency), wrapped in `[spoiler][/spoiler]` when the
format is bbcode or under a plain "Spoilers:" label otherwise. Two alternatives were
considered and rejected: letting the model guess what counts as a spoiler (unpredictable,
both over- and under-marks things), and inline markers the model is told to preserve
verbatim (fragile on a small local model, which could still paraphrase or drop the
marker). Appending after the fact sidesteps both failure modes entirely, at the cost of
spoiler content always landing at the end rather than inline with the surrounding prose.
The format choice only affects this one block -- the main review body's wording is the
same either way; BBCode markup elsewhere (bold, lists, ...) was explicitly out of scope.

The Spoiler step also pre-fills itself with a best guess (`core/spoiler_predict.c`, same
prompt/parse/retry shape as `core/followup.c`): one extra, best-effort LLM call that
quotes verbatim from the notes and follow-up answers whatever sounds like a genuine plot
spoiler, or `{"spoiler": ""}` when nothing does. This is deliberately *not* the same
thing as the "let the model guess" option rejected above -- the guess never ships on its
own; it only pre-fills the field the player already edits or clears before Ctrl+S/Enter,
the same as if they'd typed it themselves. Only triggered when the field is still empty,
so it never clobbers an answer already typed (including after going back through `o`),
and a failure just leaves the field blank rather than surfacing an error for something
that was never required. Runs on its own loading screen (`TUI_SCREEN_LOADING_SPOILER_
PREDICT`), the same pattern as the follow-up questions' loading screen.

A plain keyword match was tried first (no network call, no new loading screen) but
turned out too narrow in practice: real notes describing a specific plot point rarely
contain any of a fixed list of generic words like "ending" or "twist", so it almost never
fired. The LLM call costs one extra round trip but actually generalizes to whatever the
player happened to write.

From the Result screen, the TUI also offers to go back through Liked/Disliked/Notes/
Options with everything from last time still in place (`o`) -- for fixing a wrong
like/dislike or picking a different length/tone/vocab without starting over from what the
AI extracted -- and to open the current review text in `$EDITOR` (`e`, falling back to
`vi`), replacing it with whatever's in the file once the editor exits. termbox2 has to
fully release the terminal for that (`tb_shutdown()` / `tb_init()` around the subprocess),
same as it does for a fatal signal. `c` copies it to the clipboard two ways at once, since
there's no reliable way to tell from inside the app whether either landed: an OSC 52
escape sequence written straight to stdout (no external dependency, works in most modern
terminals even over SSH/tmux), and -- belt and suspenders -- whichever of
`wl-copy`/`xclip`/`pbcopy` is actually installed, fed the text over a pipe.

### 4.4 Providers
A small vtable so backends are swappable, with an optional second entry point for
providers that can enforce a JSON schema server-side:

```c
typedef struct {
    const char *name;
    int (*complete)(const llm_cfg *cfg,
                    const char *system, const char *user,
                    strbuf *out, err_t *err);
    int (*complete_json)(const llm_cfg *cfg,            /* optional; NULL if unsupported */
                          const char *system, const char *user,
                          const char *json_schema,
                          strbuf *out, err_t *err);
} llm_provider;
```

- **anthropic**: `POST https://api.anthropic.com/v1/messages`, headers `x-api-key`, `anthropic-version: 2023-06-01`, read `content[0].text`. No `complete_json`.
- **openai** (compatible): `POST <base_url>/chat/completions`, `Authorization: Bearer`, read `choices[0].message.content`. Covers OpenAI, LM Studio, OpenRouter, and others. No `complete_json`. Since LM Studio and other local servers are a primary use of this provider, it uses the same longer (180s) request timeout as `ollama` rather than the shared 30s default -- confirmed necessary against a real LM Studio instance, where the default timed out.
- **ollama**: Ollama's *native* API, not the OpenAI-compatible one -- `POST <base_url>/api/chat` (default `http://localhost:11434`), `stream: false`, `think: false` (skips `<think>` stripping for reasoning-style models), an `options.num_ctx` from config, and `complete_json` sets `format` to the call's JSON schema so most parse failures never happen server-side. Local CPU inference can be slow, so this provider uses a longer (180s) request timeout than the shared default.

---

## 5. Architecture (C)

```
src/
  main.c            arg parsing, subcommands, app bootstrap
  config.c/.h       XDG paths, inih loading, env API keys, config_save() to write
                    config.ini back out (used by the TUI's Settings screen)
  util/
    arena.c/.h      arena allocator (one per session; frees everything at once)
    strbuf.c/.h     growable string buffer
    text.c/.h       clip, trim, strip BBCode/HTML entities, word-wrap
    json_fence.c/.h strip a ```[json] code fence an LLM wrapped its answer in,
                    shared by every module that parses a raw LLM response as JSON
    secret_crypto.c/.h  encrypt/decrypt a config.ini secret at rest (7.1); vendored
                    ChaCha20 + a machine-local key file, no external crypto dependency
  net/
    http.c/.h       libcurl wrapper: GET/POST JSON, retries, timeouts
  steam/
    steam.c/.h      search, appdetails, reviews (pagination)
    cache.c/.h      JSON files under ~/.cache/steamscribe/<appid>/
  llm/
    llm.c/.h        provider registry
    anthropic.c
    openai.c
  core/
    themes.c/.h     build extraction prompt, parse themes JSON; themes_add_custom_item
                    and themes_move_item for editing the checklist by hand
    followup.c/.h   build/parse the optional follow-up-questions call (4.2); best-effort,
                    never a hard failure
    review.c/.h     build generation prompt
    pipeline.c/.h   shared fetch-or-cache/call-the-LLM steps, used by both main.c and tui/
  tui/
    state.c/.h      pure state machine (screens, keys -> actions, result folding) -- no
                    termbox2 dependency, unit-tested without a terminal. Every text field
                    (search, custom checklist items, notes, follow-up answers, Settings)
                    shares one text_field_t { buf, cap, *len, *cursor } and a handful of
                    editing primitives (insert/backspace/delete-forward/left/right/home/
                    end at an arbitrary cursor position, not just the end) -- see 5.5.
                    Includes a Settings screen (F2) covering every config.ini setting
                    (provider/model/base_url/num_ctx, steam language/review-count/cache-
                    ttl/id/api-key, review length/tone/vocab defaults) except the
                    Anthropic/OpenAI keys, which stay env-only -- staged in app_state_t
                    and only written into the live config_t (then config_save()'d) on
                    Ctrl+S; Esc discards the edit. The Steam key field is masked on
                    screen except while actively being edited, and only overwrites
                    config_t's copy (clearing its env-sourced flag) if its staging buffer
                    actually differs from the live value when you save -- so leaving it
                    alone while editing something else never copies an env-sourced key
                    into config.ini
    keys.h          the handful of termbox2 TB_KEY_* values state.c reacts to, duplicated
                    as plain macros so state.c doesn't need to include termbox2.h
    app.c/.h        termbox2 init/shutdown (incl. signal handling) and the main loop:
                    draws state, performs blocking IO for the Loading screens, otherwise
                    polls a key and feeds it to state.c. Also where $EDITOR integration
                    (do_edit_review()) and clipboard copy (do_copy_review(), 4.3) live --
                    no separate editor.c/clipboard.c, both turned out small enough to
                    keep inline
    widgets.c/.h    list, checklist, text area (hard-wrapped, cursor-aware -- see 5.5),
                    read-only wrapped text view, status bar
    termbox_impl.c  the one translation unit that #defines TB_IMPL
```

**Layering rule:** `tui/` depends on `core/`, and `core/` depends on `steam/` and `llm/`, which depend on `net/` and `util/`. Nothing below `tui/` knows about the terminal, so every feature works from the CLI subcommands too, which makes testing easy.

### 5.1 Core data types

```c
typedef struct { uint32_t appid; char *name; char *short_desc; char *genres; } game_info;

typedef struct { char *text; bool voted_up; int votes_up; int playtime_min; } steam_review;
typedef struct { steam_review *items; size_t count; char *score_desc; } review_set;

typedef struct { char *id; char *label; bool checked; } theme;
typedef struct { theme *liked; size_t n_liked; theme *disliked; size_t n_disliked; } theme_set;

typedef enum { LEN_SHORT, LEN_MEDIUM, LEN_LONG } review_len;
typedef enum { TONE_CASUAL, TONE_NEUTRAL, TONE_ENTHUSIASTIC } review_tone;

typedef struct {
    const game_info *game;
    const theme_set *themes;   /* only checked ones are used */
    const char *notes;
    bool recommend;
    review_len len;
    review_tone tone;
} review_request;
```

### 5.2 Memory
Use one **arena per session**. All Steam data, parsed JSON copies, and prompts are allocated from it and freed together on exit or "new game". This avoids most `free()` bookkeeping, which is the main pain point of doing this in C.

### 5.3 Concurrency
A worker `pthread` runs each Loading screen's fetch/extract/generate step (pulled forward
from the original M5 plan once local-model inference turned out slow enough on CPU that a
frozen, un-interruptible "Loading…" screen wasn't acceptable). The UI thread polls a
mutex-guarded `done` flag, animates a spinner, and stays responsive to Ctrl+C; it never
touches the network or the LLM itself. Only the background thread allocates from the
session arena while it runs -- the UI thread only reads fields the job hasn't filled in
yet -- so this works without making the arena allocator itself thread-safe.

### 5.4 Errors
Functions return `int` (0 = ok) and fill an `err_t { int code; char msg[256]; }`. The TUI shows `msg` in a status bar. `--verbose` writes HTTP and LLM traffic to `~/.cache/steamscribe/debug.log`, with API keys redacted.

`net/http.c` is the one place every Steam and LLM request goes through, so its error wording is where "offline", "rate-limited" and "bad/missing key" become friendly for every caller at once, instead of each one repeating the same status-code checks: a curl-level failure that specifically looks like no connectivity (couldn't resolve host, couldn't connect, timed out, TLS handshake failed) says so plainly rather than quoting curl's own phrasing; HTTP 401/403 says to check the API key; 429 says to wait and retry (on top of the existing automatic retry-with-backoff for 429/5xx); 5xx says the server's having trouble. A missing Anthropic/OpenAI key is caught even earlier, in `pipeline_resolve_llm()`, before any request is sent -- except openai against a local `base_url` (LM Studio etc.), which never needed a key to begin with.

Whatever's currently in the status bar is also flushed to stderr once the terminal tears
down -- on a normal quit, on Ctrl+C mid-loading screen, and on a fatal signal (`SIGINT`/
`SIGTERM`/`SIGHUP`/`SIGQUIT`). Without this, an error like "Could not load game: ..." was
only ever visible on the alternate screen the TUI was using, so leaving the app via
Ctrl+C (the obvious reflex when something's gone wrong) wiped it along with everything
else. The signal handler has no arguments of its own, so `tui_run()` keeps a static
pointer (`g_signal_state`) to the live `app_state_t` for it to read; it writes with the
raw `write()` syscall rather than `fprintf`, since stdio buffering isn't async-signal-safe.

### 5.5 Text input and the cursor
Every editable field (search, custom checklist items, notes, follow-up answers, Settings)
is a `text_field_t { char *buf; size_t cap; size_t *len; size_t *cursor; }`, and
`handle_text_field_key()` in `tui/state.c` handles backspace, forward delete,
left/right/home/end, and inserting a typed character the same way for all of them --
`cursor` is a byte offset into `buf`, 0..`*len`, and insert/delete act at it, not just at
the end. Each field's own key handler only deals with what's specific to it (Enter, Esc,
Ctrl+S, numeric-only for Num ctx/Reviews).

Rendering an arbitrary cursor position correctly needs an exact, reversible mapping
between a byte offset and its on-screen (row, col) -- `widgets_draw_text_area()` gets
that by *hard*-wrapping (chopping every `width` characters) instead of word-wrapping.
Word-wrapping (`text_wrap`, still used for read-only text: the status bar, error
messages, the final review) collapses or reflows whitespace, so a byte offset doesn't
land on a fixed screen position any more once a line has wrapped -- that mismatch was
exactly an earlier bug: typing a space at the end of a wrapped field didn't visibly move
the cursor (word-wrap's line-splitting logic doesn't emit a word-ending space that has
nothing after it yet), so the cursor looked stuck until the next character "caught it up"
by two positions instead of one. `widgets_draw_text_area()` also splits on literal `\n`,
which is what makes the notes field properly multi-line: Enter inserts a newline once
there's something typed (an empty field still lets Enter skip straight past, so you don't
need Ctrl+S just to leave an optional field blank); Ctrl+S submits regardless of content.
This also fixed pasting multi-line text into notes: a paste's embedded newlines used to
each be read as a literal Enter keypress, which used to mean "submit and move to the next
step" -- enough embedded newlines in one paste could blow straight through
Recommend/Length/Vocab into actually generating a review. Up/down movement between
wrapped rows isn't implemented (it would need `state.c` to know the render width, which
would mean it depending on the terminal -- not worth it yet for how rarely a field wraps
to more than a couple of rows); left/right already reach every character, including
across a wrapped or newline boundary.

---

## 6. Dependencies

| Need          | Library     | How                                        |
|---------------|-------------|--------------------------------------------|
| HTTPS         | libcurl     | `find_package(CURL REQUIRED)` (system)     |
| JSON          | cJSON       | CPM                                        |
| TUI           | termbox2    | CPM, header-only (`DOWNLOAD_ONLY`)         |
| Config        | inih        | CPM, compile `ini.c` ourselves             |
| Threads       | pthreads    | `find_package(Threads)`                    |
| C utilities   | [GOBLIN](https://github.com/Nyjako/GOBLIN) | CPM, header-only (`DOWNLOAD_ONLY`); use for small reusable helpers (argv scanning, string helpers, app dirs, env, URL encoding, ...) instead of hand-rolling them |

**Why libcurl isn't fetched with CPM:** building curl from source also means building a TLS library (OpenSSL, mbedTLS, …). That's slow and fragile. Every target system already has libcurl.

### 6.1 CMake sketch

```cmake
cmake_minimum_required(VERSION 3.20)

# Some fetched dependencies declare an old cmake_minimum_required() floor that
# newer CMake refuses outright; this keeps those configuring.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(steamscribe VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(cmake/CPM.cmake)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

CPMAddPackage(
  NAME cjson
  GITHUB_REPOSITORY DaveGamble/cJSON
  GIT_TAG v1.7.19
  OPTIONS "ENABLE_CJSON_TEST OFF" "ENABLE_CJSON_UTILS OFF" "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME termbox2
  GITHUB_REPOSITORY termbox/termbox2
  GIT_TAG v2.7.0
  DOWNLOAD_ONLY YES)

CPMAddPackage(
  NAME inih
  GITHUB_REPOSITORY benhoyt/inih
  GIT_TAG r63
  DOWNLOAD_ONLY YES)

add_library(inih STATIC ${inih_SOURCE_DIR}/ini.c)
target_include_directories(inih PUBLIC ${inih_SOURCE_DIR})

CPMAddPackage(
  NAME goblin
  GITHUB_REPOSITORY Nyjako/GOBLIN
  GIT_TAG <pin-a-commit>
  DOWNLOAD_ONLY YES)

add_library(goblin INTERFACE)
target_include_directories(goblin INTERFACE ${goblin_SOURCE_DIR}/include)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.c)
add_executable(steamscribe ${SRC})
target_include_directories(steamscribe PRIVATE src ${termbox2_SOURCE_DIR})
target_link_libraries(steamscribe PRIVATE CURL::libcurl cjson inih Threads::Threads goblin)
target_compile_definitions(steamscribe PRIVATE STEAMSCRIBE_VERSION="${PROJECT_VERSION}")
target_compile_options(steamscribe PRIVATE -Wall -Wextra -Wpedantic)
```

(termbox2 needs `#define TB_IMPL` in exactly one `.c` file — not wired in yet, that
lands with the TUI in M4.)

---

## 7. Files on disk

| Path                                         | Content                         |
|----------------------------------------------|---------------------------------|
| `~/.config/steamscribe/config.ini`           | settings                        |
| `~/.cache/steamscribe/<appid>/details.json`  | appdetails response             |
| `~/.cache/steamscribe/<appid>/reviews.json`  | sampled reviews + fetch time    |
| `~/.cache/steamscribe/<appid>/themes.json`   | extracted themes (saves an LLM call) |
| `~/.local/share/steamscribe/reviews/`        | saved generated reviews (`<appid>-<date>.txt`) |
| `~/.local/share/steamscribe/secret.key`       | machine-local key for secrets stored in config.ini (0600, see 7.1) |

Respect `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` and `XDG_DATA_HOME`. Cache entries expire after `cache_ttl_hours`.

### 7.1 Secrets at rest

`[steam] steam_api_key` is the one config.ini value allowed to be a real secret (unlike
`steam_id`, which is just a numeric account id). You can paste it in as plain text; on
the very next launch, `config_load()` notices it isn't already one of this module's own
encrypted blobs, uses it as-is for that run, and rewrites the file with the encrypted
form (`util/secret_crypto.c`) -- so a plaintext key is never left sitting in config.ini
past the first launch that reads it.

The cipher is ChaCha20 (RFC 8439 block function, confidentiality only -- no Poly1305,
since this isn't authenticating a network message, just obscuring a value at rest) under
a fresh random nonce per encryption, vendored directly rather than pulled in as a
dependency (GOBLIN has base64/md5/hashing but no general-purpose cipher, and a real crypto
library felt heavier than this project's existing dependency footprint for one encrypted
config value). The key is random, generated once into `secret.key` (0600, created with
`O_EXCL` so a concurrent run can't race it into regenerating -- and overwriting -- an
existing key, which would make every value it had encrypted undecryptable) under
`goblin_appdir_data_dir()`, and reused for as long as that file exists.

**This is explicitly not a defense against someone with access to this machine's
filesystem** -- the key sits right next to the config file it decrypts, unprotected by
any passphrase, so anyone who can read one can read the other. Its only job is to keep
the key out of config.ini as plain text, so it isn't exposed by a casual `grep`, a pasted
bug report, or an accidental `git add` of a dotfiles repo -- not to withstand a
determined local attacker. `STEAM_API_KEY` (env) sidesteps this file entirely and takes
priority when set; `config_t.steam_api_key_from_env` tracks which source this run's value
came from, purely so `config_save()` (also used by the TUI Settings screen) knows never
to copy an env-sourced key into the file just because some unrelated setting changed.

---

## 8. Testing

- **Unit:** `text.c`, `strbuf`, theme JSON parsing, prompt building (golden strings). Use CTest with a tiny assert macro, no framework needed.
- **Fixtures:** saved Steam JSON responses in `tests/fixtures/` so tests run offline.
- **Fake LLM provider:** `provider = fake` returns canned JSON or text, so you can test the full pipeline without an API key.
- **`test_pipeline`:** `pipeline_resolve_llm()`'s provider/API-key resolution -- a missing key fails fast for anthropic and for openai against the real API, but not for openai against a local `base_url`, since that's the documented no-key case.
- **`test_secret_crypto`:** the ChaCha20 encrypt/decrypt round trip (including against a fixed key, so it doesn't depend on `/dev/urandom` behaving any particular way), that two encryptions of the same plaintext differ (fresh nonce), and that `secret_crypto_load_or_create_key()` is idempotent (isolated via `XDG_DATA_HOME`, same technique `test_config` already uses for `XDG_CONFIG_HOME`).
- **Sanitizers:** `-DSANITIZE=ON` builds with AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer; CI runs a separate `sanitize` job with it (pulled forward from M5).

---

## 9. Risks & mitigations

| Risk                                         | Mitigation                                           |
|----------------------------------------------|------------------------------------------------------|
| Steam changes or rate-limits endpoints       | caching, retries, fixtures, clear error message      |
| LLM returns invalid JSON                     | fence stripping, one retry, debug log                |
| Generated text sounds like AI                | short default, "plain words" rules, easy edit/regen  |
| Other players' opinions leak into the review | only checked labels and user notes are sent in step 2 |
| Text input in C TUI is fiddly                | word-wrapped input widget for free text; the generated review itself can be opened in `$EDITOR` |
| Unicode in reviews/terminal                  | treat text as UTF-8 bytes; termbox2 handles display width |
| A game's reviews overflow a local model's context | `num_ctx`-based character budget on top of the review-count cap (4.1) |

## 10. Go fallback

If C becomes a drag (most likely the TUI widgets), the architecture maps 1:1 to Go: `net/http` + `encoding/json` replace curl/cJSON, Bubble Tea + Bubbles (list, textinput, spinner) replace `tui/`, and goroutines replace the worker thread. The prompts, endpoints, cache layout and roadmap stay the same.

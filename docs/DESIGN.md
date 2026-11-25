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

All endpoints are public JSON. No key is needed.

### 3.1 Search
`GET https://store.steampowered.com/api/storesearch/?term=<q>&cc=us&l=english`
→ `items[]: { id, name, type, ... }`

### 3.2 Game details
`GET https://store.steampowered.com/api/appdetails?appids=<id>&l=english`
→ `{ "<id>": { success, data: { name, short_description, genres[], categories[], developers[], release_date, ... } } }`

(User tags aren't included. Genres and description are enough context.)

### 3.3 Reviews
`GET https://store.steampowered.com/appreviews/<id>?json=1&filter=all&language=english&review_type=<positive|negative>&purchase_type=all&num_per_page=100&cursor=<c>`

- `filter=all` sorts by helpfulness, which is what we want.
- `cursor` starts as `*`, comes back in each response, and **must be URL-encoded** on the next request.
- First page includes `query_summary` (`review_score_desc`, `total_positive`, `total_negative`).
- Each review has `review` (text), `voted_up`, `votes_up`, `author.playtime_forever`, and more.

**Strategy:** fetch N/2 positive and N/2 negative (default 100 + 100). Drop very short reviews (< 40 chars) and clip each to about 1,200 chars before sending to the LLM.

### 3.4 Request etiquette
- User-Agent: `steamscribe/<version>`
- Sequential requests with a ~300 ms delay between review pages
- 10 s connect and 30 s total timeout, with 2 retries and backoff on 429/5xx

---

## 4. LLM usage

Two calls per review.

### 4.1 Theme extraction
Input: game name, short description, genres, the sampled positive and negative reviews.
Output (strict JSON):

```json
{
  "liked":    [ { "id": "art",      "label": "Art style & lighting" } ],
  "disliked": [ { "id": "raids",    "label": "Annoying enemy raids" } ]
}
```

Rules in prompt: 5–8 items per side, short labels (≤ 4 words), most common first, no duplicates, JSON only.

Parsing: strip ```` ``` ```` fences if present and parse with cJSON. On failure, retry once with "return only valid JSON". On a second failure, show an error with the raw output in a debug log.

### 4.2 Review generation
Input: game name, **only the checked labels**, the user's free-text notes, recommend yes/no, length, tone.
System prompt rules:
- Write in first person as a regular player.
- Use only the provided points. Don't invent experiences, numbers, or features.
- Short = 2–4 sentences. Medium = 1–2 paragraphs. Long = 3–4 paragraphs.
- Plain text, no headings, no emojis unless the tone is "enthusiastic". End with a one-line verdict.

### 4.3 Providers
A small vtable so backends are swappable:

```c
typedef struct {
    const char *name;
    int (*complete)(const llm_cfg *cfg,
                    const char *system, const char *user,
                    strbuf *out, err_t *err);
} llm_provider;
```

- **anthropic**: `POST https://api.anthropic.com/v1/messages`, headers `x-api-key`, `anthropic-version: 2023-06-01`, read `content[0].text`.
- **openai** (compatible): `POST <base_url>/chat/completions`, `Authorization: Bearer`, read `choices[0].message.content`. Covers OpenAI, Ollama, LM Studio, OpenRouter, and others.

---

## 5. Architecture (C)

```
src/
  main.c            arg parsing, subcommands, app bootstrap
  config.c/.h       XDG paths, inih loading, env API keys
  util/
    arena.c/.h      arena allocator (one per session; frees everything at once)
    strbuf.c/.h     growable string buffer
    text.c/.h       clip, trim, strip BBCode/HTML entities, word-wrap
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
    themes.c/.h     build extraction prompt, parse themes JSON
    review.c/.h     build generation prompt
    pipeline.c/.h   shared fetch-or-cache/call-the-LLM steps, used by both main.c and tui/
  tui/
    state.c/.h      pure state machine (screens, keys -> actions, result folding) -- no
                    termbox2 dependency, unit-tested without a terminal
    keys.h          the handful of termbox2 TB_KEY_* values state.c reacts to, duplicated
                    as plain macros so state.c doesn't need to include termbox2.h
    app.c/.h        termbox2 init/shutdown (incl. signal handling) and the main loop:
                    draws state, performs blocking IO for the Loading screens, otherwise
                    polls a key and feeds it to state.c
    widgets.c/.h    list, checklist, line input, text view (word-wrapped), status bar
    termbox_impl.c  the one translation unit that #defines TB_IMPL
    clipboard.c/.h  OSC 52 and fallback to wl-copy / xclip / pbcopy (M5)
    editor.c/.h     open text in $EDITOR (suspend TUI, temp file) (M5)
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
- **MVP:** blocking calls with a static "Loading…" message.
- **Later:** one worker `pthread` runs fetch, analyze and generate. The UI thread polls a mutex-guarded status struct and animates a spinner. The UI thread never touches the network.

### 5.4 Errors
Functions return `int` (0 = ok) and fill an `err_t { int code; char msg[256]; }`. The TUI shows `msg` in a status bar. `--verbose` writes HTTP and LLM traffic to `~/.cache/steamscribe/debug.log`, with API keys redacted.

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

Respect `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` and `XDG_DATA_HOME`. Cache entries expire after `cache_ttl_hours`.

---

## 8. Testing

- **Unit:** `text.c`, `strbuf`, theme JSON parsing, prompt building (golden strings). Use CTest with a tiny assert macro, no framework needed.
- **Fixtures:** saved Steam JSON responses in `tests/fixtures/` so tests run offline.
- **Fake LLM provider:** `provider = fake` returns canned JSON or text, so you can test the full pipeline without an API key.
- **Sanitizers:** a `-DSANITIZE=ON` option for ASan/UBSan, run in CI.

---

## 9. Risks & mitigations

| Risk                                         | Mitigation                                           |
|----------------------------------------------|------------------------------------------------------|
| Steam changes or rate-limits endpoints       | caching, retries, fixtures, clear error message      |
| LLM returns invalid JSON                     | fence stripping, one retry, debug log                |
| Generated text sounds like AI                | short default, "plain words" rules, easy edit/regen  |
| Other players' opinions leak into the review | only checked labels and user notes are sent in step 2 |
| Text input in C TUI is fiddly                | single-line input widget; multi-line goes to `$EDITOR` |
| Unicode in reviews/terminal                  | treat text as UTF-8 bytes; termbox2 handles display width |

## 10. Go fallback

If C becomes a drag (most likely the TUI widgets), the architecture maps 1:1 to Go: `net/http` + `encoding/json` replace curl/cJSON, Bubble Tea + Bubbles (list, textinput, spinner) replace `tui/`, and goroutines replace the worker thread. The prompts, endpoints, cache layout and roadmap stay the same.

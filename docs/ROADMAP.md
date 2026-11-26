# steamscribe – Roadmap

Each milestone ends with something you can run. The TUI comes late on purpose: the whole pipeline works from the command line first, so the hard parts (Steam, the LLM, prompts) get tested before any UI work.

---

## M0 – Project skeleton
**Goal:** an empty app that builds anywhere.

- [x] Repo layout (`src/`, `tests/`, `cmake/`), `.gitignore`, `.clang-format`
- [x] `cmake/CPM.cmake` added; cJSON, termbox2, inih fetched; libcurl found
- [x] `steamscribe --version` and `--help`
- [x] `util/`: arena, strbuf, error type
- [x] CTest wired up with one dummy test
- [x] GitHub Actions: build + test on Ubuntu (Linux is the priority platform; macOS CI dropped for now)

**Done when:** a fresh clone builds with two CMake commands and CI is green.

---

## M1 – Steam client (CLI)
**Goal:** talk to Steam reliably.

- [x] `net/http`: GET with timeouts, User-Agent, retries/backoff, URL encoding (URL encoding lives at the call site via GOBLIN's `url.h`, not inside `net/http` itself)
- [x] `steamscribe search <name>` lists matches (id, name)
- [x] `steamscribe info <appid>` prints name, description, genres, review score (review score is best-effort: `info` still succeeds and notes the failure on stderr if the reviews endpoint is unreachable)
- [x] Reviews fetch with cursor pagination, positive and negative split, filtering short reviews
- [x] File cache with TTL under XDG cache dir (only `details.json` is cached so far; the review score lookup isn't cached yet since it's one cheap request)
- [x] Fixtures plus offline tests for JSON parsing

**Done when:** `steamscribe info 892970` works, and a second run is served from cache.

---

## M2 – LLM client & theme extraction
**Goal:** turn 200 reviews into a clean like/dislike list.

- [x] `config.c`: load `config.ini`, create default on first run, read API key from env
- [x] `llm/` provider registry with **anthropic** and **openai-compatible** backends (the openai-compatible backend is also the path to local models: point `base_url` at Ollama/LM Studio and leave the API key unset)
- [x] `fake` provider for tests
- [x] Theme extraction prompt plus robust JSON parsing (fence stripping, one retry)
- [x] `steamscribe themes <appid>` prints themes JSON and caches it
- [x] `--verbose` debug log with keys redacted (scoped down from "HTTP and LLM traffic" to lifecycle events -- cache hits/misses, URLs fetched, provider/model used; the `ollama` provider also now logs the raw error body on a failed call, which is what it took to diagnose the context-overflow bug below)
- [x] `num_ctx`-based character budget on top of the review-count cap, so a game with unusually long/verbose reviews can't build a theme-extraction prompt that overflows a local model's context (bug: `Resident Evil Requiem` reliably failed with "Could not load game: HTTP 400 ... exceeds the available context size")

**Done when:** themes for 3–4 different games (e.g. Valheim, a puzzle game, a shooter) look sensible, and it works with a local Ollama model. Verified against a real local LM Studio instance (`qwen/qwen3.5-9b`, loaded and served via the `lms` CLI) with the `openai` provider: both theme-extraction passes and review generation produced sensible output end to end. Not yet verified against a real Ollama server specifically (none available to test against) or Anthropic/OpenAI's hosted APIs.

---

## M3 – End-to-end MVP (no TUI)
**Goal:** first real review generated. 🎉

- [x] Review generation prompt (length, tone, recommend, notes, checked themes only)
- [x] Simple line-based interactive mode on stdin/stdout:
      numbered lists ("type numbers you liked: 1 3 4"), notes, y/n, length
- [x] Print the final review
- [x] Golden-string tests for prompt building

**Done when:** you can go from `steamscribe valheim` to a pasteable review in a plain terminal. **This is a usable v0.1.** Verified against the real Steam search/appdetails APIs and against cached data + the `fake` LLM provider end-to-end (through to the final LLM call); the live reviews endpoint and a real LLM response weren't reachable from this environment to verify the very last step.

---

## M4 – TUI
**Goal:** the nice interactive experience.

- [x] termbox2 init/teardown, resize handling, always restoring the terminal (also on signals)
- [x] Widgets: selectable list, checklist, single-line input, text view with word-wrap, status bar
- [x] Screens: Search → Select → Loading → Liked → Disliked → Notes/Options → Result
- [x] Keybindings: ↑/↓/j/k, Space toggle, Enter next, Esc back, q quit
- [x] Result actions: regenerate (r), quit (q)

**Done when:** the full flow works in the TUI, and the M3 line mode remains as `--plain`. Verified
manually in a real terminal (via tmux) end to end -- Search (typed query against the real Steam
search API) → Select → Loading → Liked/Disliked checklists → Notes/Options → Result -- including
a clean terminal restore both on a normal quit and on SIGTERM mid-session. The underlying
state-machine logic (every screen transition, keybinding, and checklist toggle) also has unit
tests that don't need a terminal at all. The live reviews fetch and a real LLM call weren't
reachable from this environment (same limitation as M2/M3), so the TUI's Loading-game and
Loading-review screens were only exercised against cached data / the `fake` provider, plus one
real (expected) failure when the reviews endpoint was unreachable, to confirm it degrades
gracefully (shows the error, doesn't crash) rather than reaching a real review.

---

## M5 – Polish
**Goal:** feels finished.

- [x] Worker thread plus animated spinner during fetch, analyze and generate (pulled forward into M4 once local-model inference turned out slow enough on CPU to need it)
- [x] Copy to clipboard (OSC 52, plus whichever of `wl-copy` / `xclip` / `pbcopy` is installed, tried in parallel since there's no reliable success signal from either)
- [x] Edit in `$EDITOR`, then return to the result screen (TUI only; `--plain` has no post-review menu to hang this off of)
- [x] Save review to `~/.local/share/steamscribe/reviews/` -- `s` on the TUI Result screen, named `<appid>-<date>.txt`; `--plain` has no post-review menu to hang this off of (same reasoning as edit-in-$EDITOR)
- [x] Add your own custom like/dislike items in the checklist
- [x] Vocabulary/style option (normal vs. simple everyday words) picked per review, right after Length, in both the TUI and `--plain` -- backed by a new `[review] default_vocab` config default (not a roadmap item originally, added on request)
- [x] Move a checklist item to the other side (liked <-> disliked), for when the AI filed something under the wrong one of your own opinions -- in both the TUI (`m`) and `--plain` (not a roadmap item originally, added on request)
- [x] "Change your answers" from the Result screen -- goes back through Liked/Disliked/Notes/Options with everything you answered last time still in place, instead of only a blind regenerate (TUI only; not a roadmap item originally, added on request)
- [x] AI-generated, optional follow-up questions (`core/followup.c`) asked right after Disliked, built from only your checked liked/disliked labels -- aimed at surfacing specific personal detail a plain "anything else?" tends to miss (e.g. "Which structure did you build that made the resource gathering feel worth it?"). Best-effort: a generation failure is just zero questions, never a hard error. In both the TUI and `--plain` (not a roadmap item originally, added on request)
- [x] Real cursor-based text editing everywhere (search, custom checklist items, notes, follow-up answers, Settings): left/right/home/end/forward-delete at an arbitrary position, not just append/backspace-at-end -- see DESIGN.md 5.5. Along the way, fixed a bug where every free-text field's cursor visibly lagged a space behind what was actually typed (word-wrap doesn't keep a trailing space, which the old cursor-placement logic relied on), made the notes field properly multi-line (Enter inserts a newline once there's content; Ctrl+S submits; an empty field still lets Enter skip past so it's not required just to leave it blank), raised its character cap from 512 to 8192, and fixed pasted multi-line text blowing straight through Recommend/Length/Vocab into generating a review (each embedded newline used to be read as "submit and advance")
- [x] Theme checklist count is a genuinely variable 7-14 instead of always landing on exactly 7 -- a small local model given only a loose "5 to 8" range in the prompt text consistently settled on the low end regardless of how many distinct points the reviews actually supported, so the schema itself (for a schema-capable provider) now also sets `minItems`/`maxItems`
- [x] `[steam] review_count` (how many player reviews to sample) is now editable from the TUI Settings screen, not just by hand-editing `config.ini`
- [x] Error messages survive quitting: whatever's in the status bar (e.g. "Could not load game: ...") is now also printed to stderr once the terminal tears down, on a normal quit, Ctrl+C, or a fatal signal -- previously it only ever lived on the alternate screen, so Ctrl+C-ing out after an error silently discarded it
- [x] Friendly errors: a missing Anthropic/OpenAI API key now fails fast with a precise message (e.g. "no Anthropic API key set -- export ANTHROPIC_API_KEY...") instead of round-tripping to an HTTP 401; a local OpenAI-compatible server (LM Studio etc., `base_url` set) is correctly left alone since it never needed a key. The shared HTTP layer (`net/http.c`, used by every Steam and LLM call) now gives specific wording for "offline" curl errors (DNS/connect/timeout), 401/403 ("check your API key"), 429 ("rate-limited... wait a bit"), and 5xx ("having trouble right now"), instead of a bare status code. Game-not-found was already friendly. Covered by `test_pipeline`
- [x] ASan/UBSan CI job, no leaks or warnings (pulled forward into a `sanitize` CI job; the whole test suite passes clean under ASan/LSan/UBSan)

**Done when:** you'd happily use it for your own reviews. **Tag v0.5.**

---

## M6 – Release
**Goal:** others can install and use it.

- [ ] README with real screenshots or an asciinema recording
- [ ] `cmake --install` support, man page or `--help` polish
- [ ] Release binaries for Linux x86_64 and macOS via GitHub Actions
- [ ] AUR package / Homebrew tap (optional)
- [ ] License, CONTRIBUTING, issue templates

**Done when:** **v1.0** tagged with downloadable binaries.

---

## M7 – Post-v0.5 extras
**Goal:** a few of the "Later / ideas" below that turned out worth doing sooner.

- [x] Rating-style output (x/10) as an option alongside the plain yes/no recommendation -- a new optional "Rating 1-10" step right after Recommend, in both the TUI and `--plain`. Blank skips it (same "Enter skips" convention as notes); the model is told to work "(N/10)" into its closing verdict line exactly as given, never to invent its own number.
- [ ] Steam BBCode output option (`[b]`, `[list]`, `[spoiler]`), as a new per-review "Format" step alongside Length/Vocab. Spoiler content is a separate, optional, verbatim step ("Any spoiler details?") appended as-is after generation rather than passed through the model -- keeps spoiler wording exactly as typed instead of relying on the model to preserve a marker or guess what counts as a spoiler.
- [ ] Optional Steam Web API key to include your playtime ("After 120 hours…") -- your own free key (`STEAM_API_KEY`, env-only like the LLM keys) plus your SteamID64 (`[steam] steam_id` in config, not secret); best-effort like the follow-up questions, silently skipped if either is missing.

**Done when:** all three above are merged.

---

## Later / ideas
- Review language selection (both for reading reviews and writing yours)
- History browser for saved reviews
- Windows native support (replace termbox2 or use a Windows console backend)

---

## Suggested order of first sessions
1. M0 in one sitting. Don't skip CI, it pays off in C.
2. M1 `search` + `info` first, then reviews pagination (cursor encoding is the tricky bit).
3. M2 with the `fake` provider first, then a real one.
4. Ship M3 and use it on a few games before starting the TUI. You'll learn which questions actually matter.

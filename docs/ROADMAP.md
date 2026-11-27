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
- [x] GitHub Actions: build + test on Ubuntu (Linux is the priority platform; macOS CI dropped for
      now, then brought back in M6 once nothing Linux-specific had crept in -- termbox2 and GOBLIN
      both already handle non-Windows platforms generically, so it built clean on `macos-latest`
      with no source changes needed)

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

- [x] README with a real asciinema recording (end-to-end against the real Steam API and a local
      Ollama model) -- `docs/demo/demo.cast`, linked from the new README "Demo" section, plus an
      animated GIF rendering of the same recording (`docs/demo/demo.gif`, via `agg`) embedded
      inline so it plays in the README directly, not just a click-through link
- [x] `cmake --install` support (installs the binary plus README/LICENSE under a normal
      prefix's `bin`/`share/doc`; cJSON's own install rules are excluded via CPM's
      `EXCLUDE_FROM_ALL` so they don't leak into the install tree). Man page / further `--help`
      polish not done yet.
- [x] Release binaries for Linux x86_64 and macOS (arm64, via `macos-latest`) built and published
      by a new `release.yml` GitHub Action on every push to `master` -- republishes a rolling
      `continuous` pre-release (see the workflow's own comment for why: no per-commit
      version-bump automation exists yet, so this isn't a tagged release per platform/arch, just
      an always-current downloadable build of master). A real tagged v1.0 is still a separate,
      later step -- that's what "Done when" below still refers to.
- [x] ~~AUR package / Homebrew tap (optional)~~ -- explicitly skipped, not worth maintaining for
      this project right now
- [x] License (MIT, matching GOBLIN's own dual MIT/Unlicense) added at the repo root.
- [x] `CONTRIBUTING.md` (branches, building, tests, code style, commit/PR conventions) and
      GitHub issue templates (bug report / feature request, as issue forms under
      `.github/ISSUE_TEMPLATE/`) -- linked from the README.

**Done when:** **v1.0** tagged with downloadable binaries.

---

## M7 – Post-v0.5 extras
**Goal:** a few of the "Later / ideas" below that turned out worth doing sooner.

- [x] Rating-style output (x/10) as an option alongside the plain yes/no recommendation -- a new optional "Rating 1-10" step right after Recommend, in both the TUI and `--plain`. Blank skips it (same "Enter skips" convention as notes); the model is told to work "(N/10)" into its closing verdict line exactly as given, never to invent its own number.
- [x] Steam BBCode output option, scoped to spoilers specifically: a new per-review "Format: (p)lain / (b)bcode" step right after Vocab, in both the TUI and `--plain`, followed by an always-asked, optional, verbatim "Spoiler details" step. The spoiler text is never passed to the model at all -- it's appended to the generated review exactly as typed, after generation, wrapped in `[spoiler][/spoiler]` when the format is bbcode or under a plain "Spoilers:" label otherwise. The main review body itself isn't affected by the format choice; only the spoiler block's wrapping changes.
- [x] Optional Steam Web API key to include your playtime ("After 120 hours…") -- your own free key (`STEAM_API_KEY`, env-only like the LLM keys) plus your SteamID64 (`[steam] steam_id` in config, not secret); best-effort like the follow-up questions, silently skipped if either is missing or the request fails. Fetched lazily right before generating (not during the game-load fetch), via `IPlayerService/GetOwnedGames`.
- [x] Spoiler step now pre-fills itself with a best guess (`core/spoiler_predict.c`, one extra best-effort LLM call) -- quotes verbatim from your notes and follow-up answers whatever sounds like a genuine plot spoiler -- that you edit or clear like anything else you typed; the field you confirm is still the only thing that ever ships, never the guess on its own. Only triggered when the field's still empty, in both the TUI (its own loading screen) and `--plain`. A plain keyword match was tried first but turned out too narrow to actually fire on real notes.
- [x] The Steam Web API key can now also be pasted straight into `[steam] steam_api_key` in config.ini (not just `STEAM_API_KEY` env) -- encrypted at rest on the next launch with a vendored ChaCha20 cipher and a machine-local key file (`util/secret_crypto.c`; see DESIGN.md 7.1 for what this does and doesn't protect against). The TUI Settings screen (F2) now also covers every other config.ini setting that wasn't editable there yet: steam language/review-count/cache-ttl/id/api-key, and the review length/tone/vocab defaults.
- [x] Browse your own Steam library sorted by playtime instead of typing a game name to search for -- **F3** from anywhere, or **n** from a finished review, opens a Library screen (same j/k/Enter navigation as Select) fed by `IPlayerService/GetOwnedGames`. Picking a game, finishing a review, then picking another loops right back to the list; `n`/F3 reset the in-progress review's notes/rating/spoiler/follow-up state first, so nothing leaks between two games picked in the same run.
- [x] `ollama` is now the default `[llm] provider` (was `anthropic`) -- it needs no API key and runs fully locally, which fits "others can install and use it" (M6) better than requiring a hosted key just to try the app. The default config.ini template shows `anthropic`/`openai` as commented-out alternatives instead. The Ollama model is also unloaded from VRAM/RAM right after a review is generated (`keep_alive: 0`), rather than left reserved until Ollama's own idle timeout.
- [x] `docs/PROVIDERS.md`: setup steps for all three providers (getting a key, picking a model, pointing at a local server) plus a shortlist of local models worth trying with `ollama` -- linked from the README and from the Settings screen itself.
- [x] Settings screen (F2) can test its staged (not-yet-saved) provider/model/base_url/num_ctx with **Ctrl+T** -- one real, minimal call that confirms the combination actually works, not just that a key is present, before you commit to it with Ctrl+S.

**Done when:** all of the above are merged.

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

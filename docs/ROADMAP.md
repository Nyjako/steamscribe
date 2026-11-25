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
- [x] `--verbose` debug log with keys redacted (scoped down from "HTTP and LLM traffic" to lifecycle events -- cache hits/misses, URLs fetched, provider/model used; full raw request/response dumping is left for later if it turns out to be needed)

**Done when:** themes for 3–4 different games (e.g. Valheim, a puzzle game, a shooter) look sensible, and it works with a local Ollama model. The `openai` provider is implemented generically for this (no API key required when `base_url` is set), but hasn't been verified against a real local Ollama/LM Studio server in this environment -- worth a manual check before relying on it.

---

## M3 – End-to-end MVP (no TUI)
**Goal:** first real review generated. 🎉

- [ ] Review generation prompt (length, tone, recommend, notes, checked themes only)
- [ ] Simple line-based interactive mode on stdin/stdout:
      numbered lists ("type numbers you liked: 1 3 4"), notes, y/n, length
- [ ] Print the final review
- [ ] Golden-string tests for prompt building

**Done when:** you can go from `steamscribe valheim` to a pasteable review in a plain terminal. **This is a usable v0.1.**

---

## M4 – TUI
**Goal:** the nice interactive experience.

- [ ] termbox2 init/teardown, resize handling, always restoring the terminal (also on signals)
- [ ] Widgets: selectable list, checklist, single-line input, text view with word-wrap, status bar
- [ ] Screens: Search → Select → Loading → Liked → Disliked → Notes/Options → Result
- [ ] Keybindings: ↑/↓/j/k, Space toggle, Enter next, Esc back, q quit
- [ ] Result actions: regenerate (r), quit (q)

**Done when:** the full flow works in the TUI, and the M3 line mode remains as `--plain`.

---

## M5 – Polish
**Goal:** feels finished.

- [ ] Worker thread plus animated spinner during fetch, analyze and generate
- [ ] Copy to clipboard (OSC 52, fallback to `wl-copy` / `xclip` / `pbcopy`)
- [ ] Edit in `$EDITOR`, then return to the result screen
- [ ] Save review to `~/.local/share/steamscribe/reviews/`
- [ ] Add your own custom like/dislike items in the checklist
- [ ] Friendly errors (no key, offline, game not found, rate-limited)
- [ ] ASan/UBSan CI job, no leaks or warnings

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

## Later / ideas
- Optional Steam Web API key to include your playtime ("After 120 hours…")
- Steam BBCode output option (`[b]`, `[list]`)
- Review language selection (both for reading reviews and writing yours)
- Rating-style output (x/10) as an option
- History browser for saved reviews
- Windows native support (replace termbox2 or use a Windows console backend)

---

## Suggested order of first sessions
1. M0 in one sitting. Don't skip CI, it pays off in C.
2. M1 `search` + `info` first, then reviews pagination (cursor encoding is the tricky bit).
3. M2 with the `fake` provider first, then a real one.
4. Ship M3 and use it on a few games before starting the TUI. You'll learn which questions actually matter.

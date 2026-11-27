# steamscribe

[![CI](https://github.com/Nyjako/steamscribe/actions/workflows/ci.yml/badge.svg)](https://github.com/Nyjako/steamscribe/actions/workflows/ci.yml)
[![Release](https://github.com/Nyjako/steamscribe/actions/workflows/release.yml/badge.svg)](https://github.com/Nyjako/steamscribe/actions/workflows/release.yml)

> Working name. Rename freely.

A small terminal tool that helps you write a Steam review in your own words.

It looks up a game on Steam and reads a batch of player reviews to find what people usually praise and complain about. It then asks you a few quick questions based on those topics and turns your answers into a short, natural review that's ready to paste into Steam.

```
$ steamscribe valheim

  Found: Valheim (892970)
  Reading 200 player reviews... done

  What did YOU like?            What bothered you?
  [x] Art style & lighting      [x] Annoying enemy raids
  [x] Building system           [ ] Grindy progression
  [x] Co-op with friends        [ ] Inventory management
  [ ] Sailing & exploration     [ ] Late-game performance

  Which base did you build that you were proudest of?
  > A longhouse overlooking the ocean, took three in-game days

  Anything else? > great with friends, a bit lonely solo
  Recommend it? (y/n) > y
  Length: (s)hort / (m)edium / (l)ong > s
  Words: (n)ormal / (s)imple > n

  ── Your review ──────────────────────────────
  I really like the art style. It's simple but looks
  beautiful... Recommended, especially for co-op!
  ─────────────────────────────────────────────
  [c] copy  [e] edit  [r] regenerate  [o] change answers  [s] save  [q] quit
```

## Status

🚧 Early development. Both the full TUI and the `--plain` line-based flow shown above work end-to-end: search, reviews, checklist (including adding your own items or moving one to the other side), optional AI-generated follow-up questions, notes/options, and a generated review you can edit in `$EDITOR`, copy to clipboard, or regenerate -- the checklist's two-column layout pictured above is still just the aspirational look, and saving a copy to disk is still ahead. See [ROADMAP.md](ROADMAP.md) for the plan and [DESIGN.md](DESIGN.md) for how it works.

## Demo

Recorded end-to-end against the real Steam API and a local Ollama model: searching for a game, checking off what you liked/disliked, a couple of AI-generated follow-up questions, and the resulting review.

![steamscribe demo](docs/demo/demo.gif)

That's a GIF rendering of the real recording -- for the exact byte-for-byte original (with real terminal timing), watch it on asciinema instead:

[![asciicast](https://asciinema.org/a/szWhZOo372xvVdWo.svg)](https://asciinema.org/a/szWhZOo372xvVdWo)

or play it locally from this repo, no network needed:

```bash
asciinema play docs/demo/demo.cast
```

## Features (planned)

- Search Steam games by name
- Fetch game info and player reviews from Steam's public store endpoints, with no Steam account or key needed
- AI pulls out common likes and dislikes from the reviews
- Interactive TUI checklist to pick what *you* liked or disliked, plus free-text notes -- add your own items the AI didn't surface, or move one to the other side if the AI guessed your opinion wrong
- A few optional, AI-generated follow-up questions tailored to what you checked (e.g. "Which structure did you build that made the resource gathering feel worth it?"), so your review can include specific detail a plain "anything else?" tends to miss -- skip any you don't want to answer
- Generates a review in your chosen length, tone, and vocabulary (normal or simple, everyday words), using **only the points you confirmed**
- Optionally mentions your own playtime for the game (e.g. "After 120 hours...") if you've set your SteamID64 and a free Steam Web API key -- skipped silently if you haven't
- Press **F3** (or `n` from the finished review) to browse your own Steam library sorted by playtime instead of typing a game name -- picking one, finishing a review, then picking another loops right back to the list
- Edit the generated review in `$EDITOR`, copy it to your clipboard, regenerate, or go back and change your answers -- saving a copy locally is still ahead
- Full cursor-based text editing everywhere you type -- arrow keys, Home/End, forward delete -- and the notes field is a real multi-line box (Enter for a new line, Ctrl+S to continue)
- Works with Anthropic, any OpenAI-compatible API (e.g. LM Studio), or Ollama's own native API for local models -- Ollama is the default, since it needs no API key; see [docs/PROVIDERS.md](docs/PROVIDERS.md) for setup and a shortlist of local models that work well
- Press **F2** in the TUI to change any setting -- provider/model/base URL/context size, Steam language/review count/cache TTL/SteamID/API key, review length/tone/vocab defaults -- without leaving the app or hand-editing `config.ini`. **Ctrl+T** right there tests whatever's currently entered with one real request before you save it
- Unloads the Ollama model from VRAM/RAM right after generating a review, instead of leaving it reserved until Ollama's own idle timeout
- Caches Steam data so it doesn't hammer Steam's servers

## Requirements

- C11 compiler (GCC or Clang)
- CMake ≥ 3.20
- libcurl with TLS (system package)
- Git (CPM fetches the other dependencies automatically)
- Linux or macOS. Windows only through WSL for now.

On Arch: `sudo pacman -S base-devel cmake curl git`
On Debian/Ubuntu: `sudo apt install build-essential cmake libcurl4-openssl-dev git`
On macOS: `brew install cmake curl`

## Build

```sh
git clone https://github.com/<you>/steamscribe.git
cd steamscribe
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/steamscribe --help
```

Or, with `make` (release build, same as above):

```sh
make build
./build/steamscribe --help
make clean   # removes build/
```

## Configuration

Config file: `~/.config/steamscribe/config.ini` (created on first run). See
[docs/PROVIDERS.md](docs/PROVIDERS.md) for how to set up each of the three providers
(getting an API key, picking a model, pointing at a local server) and a shortlist of local
models known to work well.

```ini
[llm]
; See docs/PROVIDERS.md for how to set each of these up -- ollama is the default since
; it needs no API key and runs fully locally; swap in one of the commented blocks below
; for a hosted provider instead.
provider = ollama              ; anthropic | openai | ollama
model    = qwen3.5-9b          ; swap for whatever local model you've pulled -- see
                                ; docs/PROVIDERS.md for a few known to work well
; num_ctx  = 8192               ; ollama only -- raise this if reviews get truncated
; base_url = http://localhost:11434     ; ollama only; this is already the default

; provider = anthropic          ; needs ANTHROPIC_API_KEY in your environment
; model    = claude-haiku-5-5

; provider = openai             ; needs OPENAI_API_KEY, unless base_url below points
; model    = gpt-4o-mini        ; at a local OpenAI-compatible server (LM Studio, ...)
; base_url = http://localhost:1234/v1

[steam]
language    = english
review_count = 200            ; reviews to analyze (half positive, half negative)
cache_ttl_hours = 24
; steam_id = 76561198000000000  ; your own SteamID64 -- lets the review mention your
; playtime for the game. Also needs a Steam Web API key -- either STEAM_API_KEY below,
; or paste it in plain right here as steam_api_key = ... and it's encrypted at rest on
; the next launch (see docs/DESIGN.md 7.1). Both steam_id and a key are optional; the
; review just skips playtime if either is unset.

[review]
default_length = short        ; short | medium | long
default_tone   = casual       ; casual | neutral | enthusiastic
default_vocab  = normal       ; normal | simple (plain, everyday words)
```

Everything above, including `steam_api_key`, can also be edited from the TUI's Settings
screen (**F2**) instead of hand-editing the file -- and tested right there with **Ctrl+T**
before you save, which makes one real request with whatever's currently in the
provider/model/base URL fields and reports back success or the actual error.

Anthropic/OpenAI keys, read only from the environment (never stored in the config):

```sh
export ANTHROPIC_API_KEY=...   # provider = anthropic
export OPENAI_API_KEY=...      # provider = openai (not needed for local Ollama)
export STEAM_API_KEY=...       # optional -- only needed for playtime, alongside steam_id above;
                                # paste it into config.ini's steam_api_key instead if you'd rather not
                                # set an env var -- see above
```

## Usage

```sh
steamscribe                    # interactive: asks for a game name
steamscribe valheim            # start with a search
steamscribe --appid 892970     # skip search

# Non-interactive helpers (useful for debugging)
steamscribe search "valheim"
steamscribe info 892970
steamscribe themes 892970      # print extracted likes/dislikes as JSON
```

## How your review stays yours

Other players' reviews are only used to **suggest topics to ask you about**. The generated review contains only the points you checked or typed yourself, plus your recommendation. Nothing is posted automatically: you copy the text and submit it on Steam yourself.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) -- branches, building, tests, code style, and how
PRs/issues are handled.

## License

MIT -- see [LICENSE](LICENSE).

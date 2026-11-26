# steamscribe

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

🚧 Early development. Both the full TUI and the `--plain` line-based flow shown above work end-to-end: search, reviews, checklist (including adding your own items or moving one to the other side), notes/options, and a generated review you can edit in `$EDITOR` or regenerate -- the checklist's two-column layout pictured above is still just the aspirational look. Clipboard copy and saving to disk are still ahead. See [ROADMAP.md](ROADMAP.md) for the plan and [DESIGN.md](DESIGN.md) for how it works.

## Features (planned)

- Search Steam games by name
- Fetch game info and player reviews from Steam's public store endpoints, with no Steam account or key needed
- AI pulls out common likes and dislikes from the reviews
- Interactive TUI checklist to pick what *you* liked or disliked, plus free-text notes -- add your own items the AI didn't surface, or move one to the other side if the AI guessed your opinion wrong
- Generates a review in your chosen length, tone, and vocabulary (normal or simple, everyday words), using **only the points you confirmed**
- Edit the generated review in `$EDITOR`, regenerate, or go back and change your answers -- or copy to clipboard / save locally (still ahead)
- Works with Anthropic, any OpenAI-compatible API (e.g. LM Studio), or Ollama's own native API for local models
- Press **F2** in the TUI to change provider/model/base URL/context size without leaving the app or hand-editing `config.ini`
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

Config file: `~/.config/steamscribe/config.ini` (created on first run)

```ini
[llm]
provider = anthropic          ; anthropic | openai | ollama
model    = claude-haiku-5-5
; base_url = http://localhost:1234/v1   ; for LM Studio (provider = openai)
; For a local Ollama model instead, use provider = ollama -- it talks to
; Ollama's own API (not the OpenAI-compatible one), so it can set num_ctx
; and force valid JSON output, and defaults to http://localhost:11434.
; num_ctx  = 8192               ; ollama only -- raise this if reviews get truncated

[steam]
language    = english
review_count = 200            ; reviews to analyze (half positive, half negative)
cache_ttl_hours = 24

[review]
default_length = short        ; short | medium | long
default_tone   = casual       ; casual | neutral | enthusiastic
default_vocab  = normal       ; normal | simple (plain, everyday words)
```

API key, read from the environment (never stored in the config):

```sh
export ANTHROPIC_API_KEY=...   # provider = anthropic
export OPENAI_API_KEY=...      # provider = openai (not needed for local Ollama)
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

## License

TBD (MIT suggested).

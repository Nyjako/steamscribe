# Provider setup

steamscribe needs one LLM provider configured in `[llm]` in `config.ini` (**F2** in the TUI
also edits all of this). This covers how to set up each of the three, plus a shortlist of
local models worth trying with `ollama`.

Whichever you pick, **test it before relying on it**: on the Settings screen (**F2**), after
entering a provider/model/base URL, press **Ctrl+T**. That makes one real, minimal call with
exactly what's currently in those fields (even if you haven't saved yet) and reports back
either "Connection OK" or the actual error -- a typo'd model name or an unreachable server
shows up right there, not three screens later as a failed review.

---

## ollama (local, no API key -- the default)

Runs entirely on your own machine; nothing leaves it, and there's no key to manage.

1. Install ollama: see <https://ollama.com/download> for your platform (or your package
   manager -- it's packaged on most Linux distros and available via Homebrew on macOS).
2. Pull a model, e.g.:
   ```sh
   ollama pull qwen3.5-9b
   ```
3. In `config.ini`:
   ```ini
   [llm]
   provider = ollama
   model    = qwen3.5-9b
   ; num_ctx = 8192   ; raise this if reviews get truncated on a game with long reviews
   ```
   `base_url` defaults to `http://localhost:11434` -- only set it if ollama is running
   somewhere else.

This is the provider that gets `pipeline_unload_llm_model()`'d after each review (see
`docs/DESIGN.md` section 4 for why) -- Ollama frees the model from VRAM/RAM right after,
instead of keeping it reserved until its own idle timeout.

### Recommended local models

The app asks the model to follow fairly specific instructions and, for theme extraction,
to return clean JSON to a schema -- that mostly just means picking an **instruction-tuned
("chat" or "instruct") model**, not a raw base/completion model. Model quality for this
specific task hasn't been benchmarked here beyond the first entry below; treat the rest as
commonly-recommended, reasonably-sized general-purpose options worth trying, not a verified
list.

| Model (ollama tag)       | Size     | Notes                                                        |
|---------------------------|----------|---------------------------------------------------------------|
| `qwen3.5-9b`              | ~9B      | **Used and verified against this app** -- "works really good": follows the theme-extraction JSON schema reliably and writes a coherent review. |
| `qwen2.5:7b` / `qwen2.5:14b` | 7B/14B | Same family, a size down or up depending on your VRAM.        |
| `llama3.1:8b`             | ~8B      | Widely used, solid general instruction-following.              |
| `mistral:7b`              | ~7B      | Fast, smaller footprint; worth trying on tighter hardware.      |
| `gemma2:9b`               | ~9B      | Google's instruction-tuned model, similar size class to qwen3.5-9b. |
| `phi-4`                   | ~14B     | Smaller-footprint reasoning-leaning model from Microsoft.       |

A rough rule of thumb: 7-9B models run comfortably on most recent consumer GPUs with 8GB+
VRAM (or acceptably on CPU, just slower -- this is why loading-screen spinners exist).
Larger models generally extract cleaner, more varied themes, at the cost of speed.

---

## Anthropic (hosted, needs an API key)

1. Get a key at <https://console.anthropic.com> -> **API Keys**.
2. Export it (this is read from the environment only -- it's never written to `config.ini`):
   ```sh
   export ANTHROPIC_API_KEY=sk-ant-...
   ```
3. In `config.ini`:
   ```ini
   [llm]
   provider = anthropic
   model    = claude-haiku-5-5
   ```
   `claude-haiku-5-5` is fast and inexpensive and handles this app's prompts fine; swap in
   `claude-sonnet-5-5` for higher-quality extraction/writing at a higher per-review cost.

---

## OpenAI, or any OpenAI-compatible server (LM Studio, OpenRouter, ...)

The same backend covers OpenAI's own hosted API and anything that speaks the same
`/v1/chat/completions` protocol -- including local servers, which is the other path to a
local model besides `ollama` above.

**Hosted OpenAI:**
1. Get a key at <https://platform.openai.com> -> **API keys**.
2. Export it:
   ```sh
   export OPENAI_API_KEY=sk-...
   ```
3. In `config.ini`:
   ```ini
   [llm]
   provider = openai
   model    = gpt-4o-mini
   ```

**A local OpenAI-compatible server instead (LM Studio, llama.cpp's server, OpenRouter, ...):**
```ini
[llm]
provider = openai
model    = <whatever model name that server expects>
base_url = http://localhost:1234/v1   ; LM Studio's default; adjust to match your server
```
No API key is needed once `base_url` points at a local server -- an empty key just omits
the `Authorization` header.

# Contributing to steamscribe

Thanks for taking a look. This is a small personal project, still early (see
[docs/ROADMAP.md](docs/ROADMAP.md) for what's done and what's planned), so the process here
is deliberately lightweight.

For anything beyond a small fix, consider opening an issue first to talk through the
approach before putting time into a PR -- it's a much cheaper way to find out a change
doesn't fit than after it's written.

## Branches

- `master` -- the release branch. Stable, tagged.
- `dev` -- active development. **Open pull requests against `dev`, not `master`.**

Branch off `dev` for your change; one feature or fix per branch/PR is preferred over a
single PR bundling several unrelated things.

## Building

```sh
git clone https://github.com/Nyjako/steamscribe.git
cd steamscribe
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

See the main [README](README.md#requirements) for platform-specific dependency install
commands (Arch/Debian/Ubuntu/macOS).

## Running the tests

```sh
ctest --test-dir build --output-on-failure
```

Tests are offline by design: Steam responses are replayed from fixtures in
`tests/fixtures/`, and LLM calls go through a `fake` provider that returns canned
responses (see [docs/DESIGN.md](docs/DESIGN.md) section 8) -- no network access or API
keys needed to run the suite. Please keep new tests the same way; a test that needs a
live API key or network access won't run in CI.

Before opening a PR, it's also worth a clean run under AddressSanitizer/
UndefinedBehaviorSanitizer, the same job CI runs:

```sh
cmake -B build-asan -DSANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

CI (`.github/workflows/ci.yml`) runs both the normal build+test matrix (Linux and macOS)
and the sanitizer job on every PR.

## Code style

Formatting is enforced by `.clang-format` (`BasedOnStyle: LLVM`, 4-space indent, no
wrapping function arguments onto new lines) -- run `clang-format -i` on changed files
rather than hand-matching the rules. A few conventions it doesn't cover:

- `snake_case` for everything -- functions, variables, struct/typedef names (suffixed
  `_t`, e.g. `config_t`, `llm_cfg_t`), enum values (prefixed, `UPPER_SNAKE_CASE`).
- Default to **no comments**. Only add one when it explains something non-obvious the
  code itself can't -- a hidden constraint, a subtle invariant, why a workaround exists --
  never what a well-named function or variable already makes clear.
- No premature abstraction: three similar lines beat an early helper function for a
  pattern only used once or twice so far.

## Commits and pull requests

- Prefer several small, focused commits over one big one -- each should stand on its own
  and explain *why*, not just *what* (the diff already shows what changed).
- Keep a PR scoped to one feature or fix. It's fine (encouraged, even) to open several
  small PRs rather than one large one.
- Describe what you tested in the PR description -- which tests you ran, and whether you
  exercised the change manually (e.g. against a real Steam/LLM call) where that matters.
- If your change affects user-facing behavior or the architecture, update the relevant
  doc in the same PR: [README.md](README.md), [docs/ROADMAP.md](docs/ROADMAP.md) (check
  off what's now done), or [docs/DESIGN.md](docs/DESIGN.md). Docs here are expected to
  evolve alongside the code, not freeze at whatever was true when first written.

## Reporting bugs / requesting features

Use the issue templates -- they ask for the handful of details (version, provider,
`--verbose` debug log where relevant) that usually matter for a C/TUI/LLM-integration
project like this one.

## License

MIT (see [LICENSE](LICENSE)). By contributing, you agree your contribution is licensed
under the same terms.

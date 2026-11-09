# Contributing to cjq

Thanks for taking a look. `cjq` is deliberately small: a JSON query tool that
fits in three C files and has no dependencies beyond libc and libm. Patches
that keep it that way are very welcome.

## Build and test

```sh
make test          # unit + end-to-end tests
make sanitizer     # ASan + UBSan build, then the same tests
make fmt           # clang-format, if installed
```

You need a C99 compiler (GCC or Clang), `make`, and a POSIX shell. CMake also
works:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests must pass with warnings enabled. CI builds with
`-std=c11 -Wall -Wextra -Wpedantic -Werror`; please do the same locally when
you touch the C sources.

## Adding a builtin

A builtin touches four places:

1. `builtin_kind()` in `src/query.c` — declare its arity.
2. `do_call()` in `src/query.c` — implement the behavior.
3. `tests/unit.c` — at least one exact-output case.
4. `README.md`, `man/cjq.1` and the `--help` text — document it.

If the builtin takes a field path, use `eval_first(arg, item)`; it returns the
first value produced by the argument. Keep the "no result" convention: when a
builtin cannot apply (wrong type, empty array), emit nothing rather than an
error.

## Style

- C99-compatible code, four spaces, no tabs in C sources.
- Keep functions static unless they are part of `json.h`/`query.h`.
- Values and AST nodes are intentionally never freed: cjq is a one-shot CLI.
  Do not add a cleanup pass just for tidiness.
- Prefer returning early and keeping the evaluator loops flat.
- No new dependencies. If a feature needs one, it belongs in a fork.

## Commits and pull requests

- One logical change per commit; a short imperative subject line
  (`add sum(field)`, `fix slice with negative end`).
- Explain the *why* in the body when the change is not obvious.
- Include a test for every behavior change.
- Update `CHANGELOG.md` for user-visible changes.

By contributing you agree that your work is released under the MIT license in
`LICENSE`.

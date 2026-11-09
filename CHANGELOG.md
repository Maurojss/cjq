# Changelog

All notable changes to this project are documented in this file.

## [1.1.0] - 2026-11-08

### Added

- Initial release of `cjq`: a dependency-free JSON query tool in C99.
- Query language covering paths (`$.a.b`), array indexes (including
  negative indexes), array slices, `"quoted"` keys, `*` wildcards,
  recursive descent (`$..name`), optional steps (`?`), comma-separated
  alternatives (`$.a, $.b`) and pipes into builtins.
- Comparison expressions (`==`, `!=`, `<`, `<=`, `>`, `>=`), usable
  anywhere a value is expected, including inside builtin arguments such as
  `select(.age > 21)`. `==`/`!=` compare deeply; ordering follows JSON
  type and then value.
- Boolean operators `and` and `or`, with `and` binding tighter than `or`;
  both can be used inside `select()` and other builtin arguments.
- 32 builtins: `length()`, `keys()`, `values()`, `type()`, `has(key)`,
  `select(cond)`, `map(expr)`, `add()`, `contains(x)`, `startswith(s)`,
  `endswith(s)`, `join(sep)`, `split(sep)`, `unique()`, `group_by(expr)`,
  `flatten()`, `to_entries()`, `from_entries()`, `reverse()`,
  `test(regex)`, `sort(field?)`, `max(field?)`, `min(field?)`,
  `sum(field?)`, `first()`, `last()`, `floor()`, `ceil()`, `abs()`,
  `sqrt()`, `ftos()` and `not()`. `test()` uses POSIX extended regular
  expressions.
- CLI options `-c`, `-r`, `-j`, `-a`, `-s`, `-e`, `-n`, `-C`, `-M`,
  `-S`, `-f`, `-h`, `-v` (also as long names `--compact`, `--raw`,
  `--join-output`, `--ascii-output`, `--slurp`, `--exit-status`,
  `--null-input`, `--color`, `--monochrome`/`--no-color`, `--sort-keys`,
  `--from-file`, `--help`, `--version`), plus `--indent N` and `--tab`
  for pretty-print indentation.
- Exit codes: 0 (success), 1 (falsy result with `-e`), 2 (usage), 3
  (JSON input parse error), 4 (query parse error).
- Full support for `-s`/`--slurp` reading a stream of JSON values.
- Manual page in `man/cjq.1`.
- Shell completions for bash, zsh and fish, installed by `make install`.
- Builtin names and arities are validated when the query is parsed, so
  `nope()` and `length(1)` fail with exit status 4 instead of returning
  nothing.
- Unit and end-to-end test suites, runnable with `make test`; the unit
  suite also exercises 3000 random/mutated inputs against the parser.
- GitHub Actions workflows: a CI matrix (Linux/macOS, GCC/Clang), an
  ASan/UBSan job, a CMake/CTest job, a docs/completions syntax job, an
  install smoke test, and a tag-triggered release that packages the
  binary, man page and completions for Linux and macOS.
- CMake and plain `Makefile` build system invocations with `cc`.
- `make install` honours `PREFIX` and `DESTDIR`, useful for staged
  packaging.
- A 512-level nesting limit so hostile input cannot exhaust the stack.
- `CONTRIBUTING.md`, `SECURITY.md`, issue templates and a pull-request
  template.

### Design notes

- Values and AST nodes are allocated and never freed: `cjq` is a
  one-shot CLI and the OS reclaims memory at exit.
- Strict RFC 8259 JSON: `\uXXXX` escapes (including surrogate pairs),
  no comments, no trailing commas, no `NaN`/`Infinity`.
- Truthiness: `null`, `false`, `0`, `""`, `[]` and `{}` are falsy.

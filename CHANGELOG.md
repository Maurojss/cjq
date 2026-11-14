# Changelog

All notable changes to this project are documented in this file.

## [1.6.0] - 2026-11-14

### Added

- `sub(re; repl)` replaces the first POSIX extended regex match in a string
  with the literal replacement `repl`.
- `gsub(re; repl)` replaces every match; an empty match steps over one
  character instead of looping.
- `limit(n; expr)` emits at most the first `n` results of `expr`
  (`limit(3; range(100))` yields `0`, `1`, `2`).
- Builtins that take exactly two arguments (`sub`, `gsub`, `limit`) are now
  arity-checked at parse time with a dedicated diagnostic.

### Changed

- Version is now 1.6.0.
- `--help`, the README and the manual page document the new builtins.

## [1.5.1] - 2026-11-12

### Fixed

- `make` no longer fails under GNU make 3.81 (macOS) because the version
  lookup contained a `#` that the parser treated as a comment.
- The `sanitizer` target now passes `-Isrc` when building the unit suite.
- `print_value()` tolerates a NULL value instead of dereferencing it, which
  also keeps newer GCC `-fanalyzer` builds clean.

### Changed

- Version is now 1.5.1.

## [1.5.0] - 2026-11-11

### Added

- `range(n)`, `range(from; to)` and `range(from; to; step)` emit a stream of
  numbers.
- `path(expr)` returns the location of each selected value as an array of
  object keys and array indexes.
- `del(expr)` returns a copy of the input with the selected paths removed;
  multiple paths are accepted and index shifts are handled by deleting from
  the end.
- Builtin arguments are now separated by `;`, so multi-argument calls such as
  `range(0; 10; 2)` parse correctly. Commas inside an argument remain the
  alternative operator.

### Security

- `range()` stops after one million numbers, so a tiny step cannot exhaust
  memory or run indefinitely.

### Changed

- Version is now 1.5.0.
- `--help`, the README and the manual page document the new builtins.

## [1.4.0] - 2026-11-11

### Added

- `match(regex)` returns the first POSIX extended regex match as an object
  with `offset`, `length`, `string` and a `captures` array. Offsets count
  UTF-8 code points; unmatched groups report `offset: -1` and `string: null`.
- `make analyzer` runs GCC's static analyzer; CI runs it as a separate job.
- `security/` contains the scope, threat model, findings and report from a
  local security review.

### Security

- The query parser now has a 256-level recursion budget for nested builtin
  calls and dynamic indexes, preventing stack exhaustion (SEC-001).
- JSON strings containing `\u0000` are rejected with exit status 3 instead of
  being silently truncated, which could cause key collisions (SEC-004).
- GitHub Actions are pinned to full commit SHAs, the CI workflow uses
  `contents: read`, and Dependabot keeps the pins current (SEC-002).
- Added 3000 random/mutated queries to the unit suite alongside the JSON
  parser stress test.

### Changed

- Version is now 1.4.0.
- `--help`, the README, the manual page and `SECURITY.md` document the new
  builtin and security behavior.

## [1.3.0] - 2026-11-11

### Added

- `tostring()` converts any value to a string; non-strings use compact
  JSON, matching `tojson()` without the extra quoting.
- `tonumber()` parses a numeric string (surrounding blanks allowed) into
  a number. Non-numeric values produce no result.
- `ascii_downcase()` and `ascii_upcase()` change ASCII letters and leave
  UTF-8 bytes untouched.
- `ltrimstr(s)` and `rtrimstr(s)` remove a prefix or suffix when present
  and return the input unchanged otherwise.
- Unit and end-to-end coverage for all six builtins.

### Changed

- Version is now 1.3.0.
- `--help`, the README and the manual page list the new builtins.

## [1.2.0] - 2026-11-09

### Added

- Variables: `--arg NAME VALUE` defines `$NAME` as a string and
  `--argjson NAME JSON` defines it as a parsed JSON value. `$name` can be
  used anywhere a value is expected; a missing variable produces no
  result. Invalid or trailing JSON in `--argjson` is a usage error
  (exit status 2).
- `-R`/`--raw-input` reads raw text instead of JSON. Without `-s` each
  line becomes a string and the query runs once per line; with `-s` the
  whole input becomes one string. A trailing carriage return is removed
  from each line.
- `keys_unsorted()`, `min_by(expr)`, `max_by(expr)`, `fromjson()` and
  `tojson()`, bringing the builtin set to 37.
- Unit and end-to-end coverage for variables, raw input and the new
  builtins.

### Changed

- Version is now 1.2.0.
- `--help`, the README, the manual page and the bash/zsh/fish completions
  document the new options.

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

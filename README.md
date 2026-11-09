# cjq

[![CI](https://github.com/mauroncfds/cjq/actions/workflows/ci.yml/badge.svg)](https://github.com/mauroncfds/cjq/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C99](https://img.shields.io/badge/C-C99-informational)

> A tiny, dependency-free JSON query tool for the shell — jq's little
> brother, written in C99.

**cjq** reads JSON from stdin or a file, applies a small query language
(`$.users[*].name`, `$.items[-1]`, `$..id`, `$.data | length()`, …) and prints
the matching values — pretty-printed with colors on a terminal, compact or raw
for scripts.

It exists for the same reason `grep` exists next to `awk`: sometimes you want
to pull one field out of an API response in a `curl | ...` pipeline without
dragging a runtime along. No dependencies beyond libc and libm, no build-time
code generation, no configuration — one small binary that starts in
microseconds.

```
$ curl -s https://api.example.dev/orders \
    | cjq '$.data | select(.paid) | sort(.total)[-1].customer.name'
"Ada Lovelace"
```

## Why another JSON tool?

| | cjq | jq |
|---|---|---|
| Language | C99 | C |
| Runtime deps | libc + libm | libc + oniguruma (usually) |
| Learning curve | small, path-oriented | large (full functional language) |
| Startup time | µs | ms |
| Query features | paths, slices, wildcards, comparisons, booleans, 32 builtins | everything, including modules |

If you already love jq's full language, use jq. cjq is for the 90% case: read
a value, maybe walk an array, print it — with zero ceremony.

## Install

```sh
git clone https://github.com/mauroncfds/cjq.git
cd cjq
make                 # builds ./cjq
make test            # unit + end-to-end tests
sudo make install    # binary, man page and shell completions
```

With CMake instead:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

`make install PREFIX=$HOME/.local` works too, and
`make install DESTDIR=/tmp/pkg` is ready for packaging. The man page is at
`man/cjq.1` (`man ./man/cjq.1` without installing). Bash, zsh and fish
completions live in `completions/` and are installed by both build systems.

## Quick start

```sh
# one field
$ echo '{"status":"ok","count":2}' | ./cjq '$.status'
"ok"

# without the quotes
$ echo '{"status":"ok","count":2}' | ./cjq -r '$.status'
ok

# ASCII-only output, safe for logs and old terminals
$ echo '{"city":"São Paulo"}' | ./cjq -a '$.city'
"S\u00e3o Paulo"

# two-space indentation instead of the default four
$ echo '{"a":[1,2]}' | ./cjq --indent 2 '$'
{
  "a": [
    1,
    2
  ]
}

# sorted keys, for stable output and diffs
$ echo '{"b":2,"a":1}' | ./cjq -S -c '$'
{"a":1,"b":2}

# POSIX extended regex test
$ echo '"release-1.2.3"' | ./cjq -r '$. | test("^release-[0-9.]+$")'
true

# walk an array
$ echo '{"users":[{"name":"Ada"},{"name":"Linus"}]}' | ./cjq -r '$.users[*].name'
Ada
Linus

# filter with a comparison, then map
$ echo '{"users":[{"name":"Ada","age":36},{"name":"Linus","age":17}]}' \
    | ./cjq -r '$.users | select(.age > 21) | map(.name)'
["Ada"]

# missing members disappear; '?' turns the miss into null
$ echo '{"a":1}' | ./cjq '$.b'
$ echo '{"a":1}' | ./cjq '$.b?'
null

# use it as a test in shell scripts
$ echo '{"ok":false}' | ./cjq -e '$.ok'
$ echo $?
1
```

## Query language

A query is an **expression**: a list of steps applied to the input document,
optionally piped into a builtin. Whitespace between steps is ignored.

| Syntax | Meaning |
|---|---|
| `$.a.b` | object member access |
| `$.a[2]` | array item (0-based) |
| `$.a[-1]` | negative indexes count from the end |
| `$.a[1:3]` | slice from 1 (inclusive) to 3 (exclusive) |
| `$.a[1:]` / `$.a[:2]` | open-ended slices |
| `$."two words"` | quoted member names |
| `$.a[*]` or `$.a[]` | every array item / every object value |
| `$.a?` | optional step: `null` when the step misses |
| `$..uid` | every `uid` member, at any depth |
| `$.a, $.b` | output both expressions, in order |
| `$.a \| length()` | pipe a result into a builtin |
| `$.age > 21` | comparison: `==`, `!=`, `<`, `<=`, `>`, `>=` |
| `select(.age > 21)` | compare inside a builtin argument |
| `$.ok and $.age > 21` | boolean `and` / `or`; `and` binds tighter |

Notes on corner cases, by design:

- **Only literal keys.** `$.a-b` is the key `a-b`; there is no arithmetic or
  dot-notation `|` confusion.
- **Comparisons are scalar.** `==`/`!=` are deep equality (object order does
  not matter); `<`, `<=`, `>`, `>=` order by type (`null < false < true <
  numbers < strings < arrays < objects`), then by value. A comparison with a
  missing operand produces no result, which makes `select(.age > 21)` drop
  items that lack `age`.
- **Boolean operators are scalar too.** `and`/`or` evaluate the truthiness of
  each side and produce a boolean; `and` binds tighter than `or`, and both
  bind looser than comparisons but tighter than `|`.
- **`?` is "null on miss".** Unlike jq, where `?` suppresses errors, cjq uses
  it to make optional fields explicit: the step always produces either its
  normal results or a single `null`. This keeps shell pipelines branch-free.
- **Slices are forgiving.** A negative lower bound wraps from the end
  (`$.a[-2:]` is "last two"); a negative upper bound is treated as a miss
  (`$.a[0:-2]` prints nothing), because "everything up to an unknown point"
  is almost never what a script wants.
- **Pipes run per input.** `$.a[*] | length()` asks each item for its length,
  like jq, rather than turning the stream into an array first.

### Builtins

| Call | Result |
|---|---|
| `length()` | string bytes, array/object item count, 0 for `null`, absolute value for numbers |
| `keys()` | sorted member names as an array; with an argument, the member values in key order |
| `values()` | member values (objects) or items (arrays) as an array |
| `type()` | `"null"`, `"boolean"`, `"number"`, `"string"`, `"array"` or `"object"` |
| `has("key")` | `true` when the current object has that member |
| `select(cond)` | keeps array items (or the value itself) where `cond` is truthy |
| `map(expr)` | transforms each array item / object value with `expr` |
| `add()` | sums numbers, concatenates strings and arrays, merges objects |
| `contains(x)` | substring for strings, membership/subset for arrays, subset for objects |
| `startswith(s)` / `endswith(s)` | string prefix/suffix test |
| `join(sep)` | joins array strings and numbers with `sep` (`null` becomes empty) |
| `split(sep)` | splits a string on `sep` (an empty separator splits bytes) |
| `unique()` | sorted array with duplicate values removed |
| `group_by(expr)` | groups array items by `expr`, sorted by the key |
| `flatten()` | recursively flattens nested arrays |
| `to_entries()` | `{"key": k, "value": v}` entries for objects and arrays |
| `from_entries()` | inverse of `to_entries()`; string and numeric keys |
| `reverse()` | reverses an array or a UTF-8 string |
| `test(regex)` | POSIX extended regex test on a string |
| `sort(field?)` | array sorted ascending; `field` selects the sort key |
| `max(field?)` / `min(field?)` | the item with the greatest/smallest key |
| `sum(field?)` | sums numeric values (missing/non-numeric keys are skipped) |
| `first()` / `last()` | first/last array item |
| `floor()` `ceil()` `abs()` `sqrt()` | math on numbers |
| `ftos()` | number → string |
| `not()` | truthiness negation |

**Truthiness:** `null`, `false`, `0`, `""`, `[]` and `{}` are falsy; everything
else is truthy.

## Flags

| Flag | Meaning |
|---|---|
| `-c`, `--compact` | compact JSON, one line, no spaces |
| `-r`, `--raw` | print string results without quotes |
| `-j`, `--join-output` | no newline after each result |
| `-a`, `--ascii-output` | escape non-ASCII as `\uXXXX` (surrogate pairs) |
| `-s`, `--slurp` | parse a stream of JSON values into one array |
| `-e`, `--exit-status` | exit 1 when the result is falsy or empty |
| `-n`, `--null-input` | query `null`; read no input |
| `-C`, `--color` | force colors even when piped |
| `-M`, `--no-color` | never colorize (also `CJQ_NO_COLOR=1`) |
| `--indent N` | pretty-print with N spaces (0–8) |
| `--tab` | pretty-print with one tab |
| `-S`, `--sort-keys` | sort object keys in the output |
| `-f`, `--from-file` | read the query from a file instead of the argument |
| `-h`, `--help` | usage |
| `-v`, `--version` | version |

`cjq [flags] QUERY [FILE]` — with no `FILE`, input comes from stdin.
Flags and the query can be in any order.

## Exit status

| Code | Meaning |
|---|---|
| `0` | success (also: truthy result under `-e`) |
| `1` | falsy or empty result while `-e` is active |
| `2` | usage error (unknown flag, missing query) |
| `3` | invalid JSON input |
| `4` | invalid query |

## How it is built

```
src/json.c    JSON value model, recursive-descent parser, printer
src/query.c   query parser → AST → evaluator, builtins
src/main.c    CLI: flags, input, output, exit codes
tests/unit.c  white-box unit tests (engine + query language)
tests/run_tests.sh  end-to-end tests through the CLI
completions/  bash, zsh and fish completion scripts
man/cjq.1     manual page (mdoc)
```

A few implementation choices worth knowing about:

- **One arena, no frees.** Values and AST nodes are allocated and never
  released; the process exits immediately after printing. This keeps the code
  small and honest for a CLI. It is a poor design for a library — use it as
  one at your own risk.
- **Strict JSON.** Numbers follow the RFC grammar (no hex, no `NaN`), strings
  decode `\uXXXX` including surrogate pairs, duplicate object keys keep the
  first position and the last value.
- **Parser + formatter round-trip.** `query_format()` renders an AST back to
  text; the unit suite uses it as a regression net for the parser.
- **`?` is a flag on a step**, not a wrapper node, which keeps the AST flat and
  the formatter a simple walk.
- **Builtins are checked at parse time.** Unknown names and wrong arities are
  query errors (exit 4), not silent empty results.

## Testing

```sh
make test          # unit + e2e, plain build
make sanitizer     # ASan + UBSan build, unit + e2e
make fmt           # clang-format the sources, if available
```

CI runs the suite on Ubuntu and macOS with GCC and Clang, both Make and CMake,
with `-Werror`, plus an ASan/UBSan job and a man-page render check. The tests
are intentionally boring: exact bytes in, exact bytes out, plus exit codes. The
unit suite also throws a few thousand random byte strings at the parser and
checks that deep nesting is rejected instead of overflowing the stack.

## Roadmap

- [ ] more builtins: `range()`, `max_by()`, `min_by()`
- [ ] `match()` with capture offsets
- [ ] Windows build (MSVC)
- [x] `map()`, `add()`, `contains()`, `startswith()`, `endswith()`, `join()`, `split()`
- [x] `unique()`, `group_by()`, `flatten()`, `to_entries()`
- [x] `from_entries()`, `reverse()`, `test(regex)`
- [x] comparisons inside `select()` (`select(.age > 21)`)
- [x] boolean operators (`and`, `or`) inside `select()`
- [x] `-a/--ascii-output`, `--tab`, `--indent n`
- [x] `-S/--sort-keys` and `-f/--from-file`
- [x] random-input stress tests for the JSON parser
- [x] tagged releases with Linux and macOS binaries

Contributions are welcome. If you add a builtin, add a unit case and a line in
the builtin table.

## License

MIT — see [LICENSE](LICENSE).

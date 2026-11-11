# Security review report — cjq 1.4.0

- Date: 2026-11-11
- Target: `Maurojss/cjq`, version 1.4.0
- Scope: local source, tests and CI configuration; synthetic inputs only
- Authorization: repository owner
- Rules: read-only analysis first, then local dynamic tests; no third-party
  targets, no destructive payloads

## Summary

| ID | Severity | Finding | Status | Fix |
|---|---|---|---|---|
| SEC-001 | Medium | Unbounded recursion in the query parser | Fixed | 256-level query nesting budget |
| SEC-002 | Low | Mutable Actions tags and broad workflow permissions | Fixed | SHA pinning, least privilege, Dependabot |
| SEC-003 | Low | Regex evaluation cost | Accepted | Documented; use timeouts for untrusted patterns |
| SEC-004 | Low | Embedded NUL truncated JSON strings and could merge keys | Fixed | Reject `\u0000` with exit status 3 |

No Critical or High findings were identified.

## Method

1. **Inventory** — 3 C translation units, 2 test suites, 2 workflows, no
   third-party runtime dependencies (libc and libm only).
2. **Static review** — parser bounds, integer handling, recursion, string
   handling, regex use, file and stdin reads, CLI argument handling, format
   strings, build and CI configuration.
3. **Compiler analysis** — GCC 12 with `-Wall -Wextra -Wpedantic -Werror`
   and `-fanalyzer` (leak warnings disabled because the arena is one-shot).
4. **Dynamic tests** — unit and end-to-end suites under UBSan; 3000 random
   JSON documents and 3000 random/mutated queries; targeted deep-nesting and
   NUL-byte cases.
5. **Secret scan** — checked the tree for private keys, GitHub tokens, AWS
   keys and credential assignments. No secrets or sensitive files found.

## Results

```
unit:      283 checks, 0 failures
e2e:       133 checks, 0 failures
UBSan:     clean (unit and e2e)
-fanalyzer: clean
secrets:   none found
```

ASan could not complete in the isolated review sandbox; the CI job builds and
runs with `address,undefined` and disables only LeakSanitizer, which would
flag the intentional one-shot allocations.

## Fix verification

- Deep queries now fail with exit status 4 and `query nesting too deep`
  instead of exhausting the stack; regression tests cover builtin arguments
  and dynamic indexes.
- `printf '%s' '"a\u0000b"' | cjq '$'` exits 3 with
  `NUL is not supported in strings`.
- Workflows reference full-length action SHAs, `ci.yml` has
  `permissions: contents: read`, and Dependabot tracks the pins.

## Residual risks

- **Regex cost** (SEC-003): patterns are caller-authored code; use a
  wall-clock timeout when they come from an untrusted source.
- **Memory use** is proportional to input size; cjq is not a streaming
  multi-gigabyte processor.
- **C-string model**: the `\u0000` rejection is a deliberate fail-closed
  policy. Any future length-aware string type must preserve or replace it.
- **GitHub account and infrastructure** were out of scope.

## Files

- [`scope.md`](scope.md)
- [`threat-model.md`](threat-model.md)
- [`findings/`](findings/)

# SEC-003 — Regex evaluation cost

| Field | Value |
|---|---|
| Severity | Low |
| Status | Accepted / documented |
| CWE | CWE-1333: Inefficient Regular Expression Complexity |
| Asset | `test(regex)` and `match(regex)` |
| Authentication | None; requires control of the pattern |

## Preconditions

The query contains a pathological regular expression and the input string is
large. The caller controls both the pattern and the input.

## Impact

A slow match could delay the process. `cjq` uses POSIX extended regular
expressions from the platform libc, which do not support backreferences; the
practical risk is lower than with backtracking engines, but complex patterns
can still be costly on large inputs.

## Position

The query text is treated like shell code: the caller already decides what
the process does. `cjq` cannot impose a meaningful timeout inside a single
`regexec()` call without threads or signals, and adding one would complicate
a small CLI.

## Mitigation

- Run `cjq` with a wall-clock timeout when the pattern or input is untrusted.
- Keep input size bounded at the wrapper level.
- Prefer literal string operations (`startswith`, `contains`, `ltrimstr`)
  for untrusted patterns.

## References

- CWE-1333: https://cwe.mitre.org/data/definitions/1333.html
- POSIX `regexec(3)`

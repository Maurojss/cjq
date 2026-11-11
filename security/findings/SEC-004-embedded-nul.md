# SEC-004 — Embedded NUL bytes truncated JSON strings

| Field | Value |
|---|---|
| Severity | Low |
| Status | Fixed (fail closed) |
| CWE | CWE-170: Improper Null Termination |
| Asset | JSON parser (`src/json.c`) |
| Authentication | None; requires control of the JSON document |

## Preconditions

The input contains a JSON string with the escape `\u0000`, for example:

```json
{"a\u0000b":1,"a":2}
```

## Impact

Strings were stored as C strings, so an embedded NUL ended the value early.
The document above collapsed to `{"a":2}`: a key collision that could
mislead a caller inspecting untrusted JSON. It could not cause memory
corruption or code execution.

## Fix

`\u0000` is now rejected while parsing, with exit status 3 and the message
`NUL is not supported in strings`. This is a deliberate fail-closed policy:
rejecting a rare valid JSON construct is safer than silently truncating it.

Regression coverage:

```sh
printf '%s' '"a\u0000b"' | ./cjq '$'
# cjq: JSON: NUL is not supported in strings (line 1, column 4)
# exit 3
```

## Residual risk

The string model remains C-string based, so this policy must be preserved if
the parser is extended. A future length-aware string type would allow the
construct safely.

## References

- CWE-170: https://cwe.mitre.org/data/definitions/170.html
- RFC 8259, Section 7 (strings)

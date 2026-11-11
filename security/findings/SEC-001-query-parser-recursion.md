# SEC-001 — Unbounded recursion in the query parser

| Field | Value |
|---|---|
| Severity | Medium |
| Status | Fixed |
| CWE | CWE-674: Uncontrolled Recursion |
| Asset | Query parser (`src/query.c`) |
| Authentication | None; requires control of the query text |

## Preconditions

An attacker can supply query text to `cjq`, directly or through a wrapper
that accepts user-defined filters.

## Reproduction

Generate a query with 1000 nested builtin calls:

```sh
awk 'BEGIN{for(i=0;i<1000;i++)printf "map("; printf "."; for(i=0;i<1000;i++)printf ")"; print ""}' > deep.cjq
printf '{}' | ./cjq -f deep.cjq
```

The same shape works with nested dynamic indexes: `.[.[.[...]]]`.

Before the fix the parser recursed once per nesting level and could exhaust
the process stack. The JSON parser already had a 512-level limit, but the
query parser did not.

## Impact

A crafted query could crash the process (stack exhaustion), causing a denial
of service in any wrapper that exposes queries to untrusted users. An
attacker cannot execute code through this issue.

## Fix

- Added `parse_subquery()` with a 256-level recursion budget for builtin
  arguments and dynamic indexes.
- Exceeding the budget fails cleanly with exit status 4 and the message
  `query nesting too deep`.
- Added regression tests for deep builtin and index queries.
- Added 3000 random/mutated queries to the unit suite.

## Verification

```sh
printf '{}' | ./cjq -f deep.cjq
# cjq: query: query nesting too deep (line 1, column 1029)
# exit 4
```

## References

- CWE-674: https://cwe.mitre.org/data/definitions/674.html
- OWASP ASVS 5.2.1 (input validation / resource limits)

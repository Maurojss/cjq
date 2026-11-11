# Threat model

## Assets

- Integrity of the `cjq` process and its output.
- JSON documents supplied by the caller, which may contain sensitive data.
- The developer workstation and CI runners used to build the tool.

## Trust boundaries

| Boundary | Data | Trust |
|---|---|---|
| CLI arguments | flags, query text, file names, `--arg` values | authored by the caller |
| JSON input | documents from stdin or a file | untrusted |
| Raw input | arbitrary lines of text | untrusted |
| Filesystem | query and input files selected by the caller | caller-controlled path |
| stdout/stderr | results and diagnostics | may be consumed by scripts |
| CI | repository contents, Actions workflows | supply-chain surface |

`cjq` does not evaluate code, open network connections, or write files. Its
main risks are parser memory safety, unbounded recursion, resource
exhaustion, misleading output, and CI supply-chain compromise.

## STRIDE summary

| Threat | Relevance | Notes |
|---|---|---|
| Spoofing | Low | no authentication or identity in scope |
| Tampering | Medium | malformed JSON/query must not corrupt memory or output |
| Repudiation | Low | no audit trail expected from a CLI |
| Information disclosure | Low | output is the caller's own input; no secrets stored |
| Denial of service | Medium | deep nesting, huge inputs, expensive regexes |
| Elevation of privilege | Low | no privileged operations; runs as the caller |

## Security assumptions

- The query text is authored by the caller, like a shell command.
- JSON input can be hostile and must fail closed with a diagnostic.
- A one-shot process may leave memory to the OS at exit; this is not a leak.

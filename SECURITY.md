# Security policy

## Supported versions

The latest release on the `main` branch is supported. Older releases may not
receive fixes.

## Reporting a vulnerability

Please do **not** open a public issue for a vulnerability. Use GitHub's private
vulnerability reporting (Security → Report a vulnerability) or email the
maintainer at `269823277+Maurojss@users.noreply.github.com`.

Include:

- the version or commit you tested;
- a minimal input and command line that reproduces the problem;
- the observed behavior, and whether it is a crash, hang, memory error or
  something else;
- your platform and compiler, if relevant.

You can expect an acknowledgement within a few days. Once a fix is available,
we will coordinate disclosure and credit you unless you prefer otherwise.

The scope of the latest local review, its threat model and its findings are in
[`security/report.md`](security/report.md).

## Hardening

- JSON nesting is capped at 512 levels and query nesting at 256 levels, so
  hostile documents and queries fail cleanly instead of exhausting the stack.
- `range()` emits at most one million numbers per call, bounding output from a
  tiny step or a huge interval.
- Strings containing `\u0000` are rejected with exit status 3 rather than
  silently truncated; the C-string model cannot represent embedded NUL safely.
- CI workflows use least-privilege permissions, pinned Actions and a static
  analysis job.
- `test(regex)` and `match(regex)` use POSIX extended regular expressions.
  Treat patterns from untrusted sources like code and run with a wall-clock
  timeout.

## Scope

`cjq` parses untrusted JSON and query text, so parser robustness is in scope:
out-of-bounds reads/writes, unbounded recursion, integer overflow, hangs and
memory exhaustion. The tool does not evaluate code, write files or open
network connections; it only reads one input stream and writes to stdout.

The following are known and intentional, not vulnerabilities:

- **Memory is not freed.** Values and AST nodes live until the process exits.
  This is a deliberate one-shot CLI design, not a leak in a long-lived server.
- **Input is read into memory.** `cjq` is not designed to stream multi-gigabyte
  documents; memory use is proportional to the input.
- **Nesting is capped at 512 levels** for JSON and 256 levels for queries, to
  keep recursion bounded. Deeper input is rejected.

# SEC-002 — Mutable GitHub Actions tags and broad workflow permissions

| Field | Value |
|---|---|
| Severity | Low |
| Status | Fixed |
| CWE | CWE-1357: Reliance on Insufficiently Trustworthy Component |
| Asset | CI/CD workflows (`.github/workflows/`) |
| Authentication | Requires compromise of an upstream action or tag |

## Preconditions

A workflow references an action by a mutable tag such as `@v4`. If the
upstream tag is moved to a malicious commit, the next build executes it with
the workflow token.

The `CI` workflow also had no explicit `permissions` block, so its jobs
received the repository default token permissions.

## Impact

Code execution in CI with access to repository contents and, depending on
the token default, write permissions. This is a supply-chain risk rather
than a flaw in the `cjq` binary.

## Fix

- Pinned `actions/checkout` and `softprops/action-gh-release` to full commit
  SHAs, with the human-readable version in a comment.
- Added top-level `permissions: contents: read` to `ci.yml`; the release
  workflow keeps only `contents: write`.
- Added `.github/dependabot.yml` to keep the pinned Actions current.

## Verification

- `git grep -n "uses:" .github/workflows` shows SHAs only.
- Workflow syntax parses and CI jobs retain the permissions they need.

## References

- CWE-1357: https://cwe.mitre.org/data/definitions/1357.html
- OpenSSF Scorecard: Pinned-Dependencies

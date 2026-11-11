# Security review scope

## Target

- Project: `cjq`, a dependency-free JSON query CLI in C99.
- Repository: `Maurojss/cjq` (private during the review).
- Environment: local Linux workspace, synthetic data only.
- Authorization: the repository owner requested this review.

## In scope

- C sources under `src/`, the unit and end-to-end tests, and build files.
- Input handling: CLI arguments, query text, JSON documents, raw-input mode.
- Parser robustness: bounds, integer handling, recursion, hangs.
- Build and CI configuration: compiler flags, GitHub Actions supply chain.
- Static analysis and local fuzzing.

## Out of scope

- GitHub-hosted infrastructure and account security.
- Third-party services and network behavior.
- Destructive testing, denial-of-service against live systems, or tests on
  data that does not belong to the owner.
- Formal verification and cryptographic review (the tool uses no crypto).

## Constraints

- Read-only analysis first; dynamic tests only against local binaries.
- No real credentials or personal data in findings or commits.
- Reproduce each issue with the smallest input that demonstrates it.

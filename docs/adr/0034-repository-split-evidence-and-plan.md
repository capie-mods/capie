# ADR-0034: Split into four repositories; evidence stays in the archived monorepo

- Status: Accepted. Split prepared (`tools/repo-split/`), not executed yet.
- Date: 2026-10-14

## Context

The monorepo is being split into `capie`, `capie-script-sdk`, `capie-design` and
`rename-anything` under the `capie-mods` GitHub organization
([plan](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261014/repo-split/plan.txt)). The evidence archive in
`docs/evidence` contains about 2000 absolute paths with the maintainer's Windows account name.
Its files are referenced by SHA-256 in reports, so rewriting them would break those references.
The organization is on GitHub Free, where branch protection and rulesets do not apply to
private repositories.

## Decision

- The new repositories get code and documentation without `docs/evidence`. The full evidence
  archive stays in the private, archived `hoi4-rename-anything` monorepo, which keeps the
  complete history and the files the pinned hashes point to.
- The organization stays on GitHub Free. Branch protection is switched on when the
  repositories become public; until then the private repositories have none.

## Consequences

- Public repositories do not expose local account paths, and no evidence bytes change.
- Documentation in the new repositories must not link into `docs/evidence`; links point to the
  archived monorepo or are removed.
- Until publication, history on `main` in the new repositories can be force-pushed by any admin;
  work stays local-commit-first as today.

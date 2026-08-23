# Zero-budget development workflow

This repository is public, so standard GitHub-hosted Actions used here do not consume the private Foundry's monthly Actions allowance. Use that separation deliberately.

## Normal development loop

1. Make product changes in the private authoritative WinInspect development repository.
2. Commit the exact private source state that should become a public build candidate.
3. Generate a constructive source projection from that clean private commit.
4. Import the validated projection into a branch of this public build/deployment repository.
5. Push the public projection branch; public CI validates the receipt, builds, and tests it on public GitHub-hosted runners.
6. When ready, create an immutable public release from the projected source. The release includes the projection receipt and product lifecycle evidence as attested subjects.
7. Let public Windows Package Foundry exercise package-manager-specific client lifecycles against those exact release subjects.
8. Let the private Foundry make the eligibility/promotion decision from immutable public evidence.
9. Project only the sanitized approved package model back into public Foundry distribution surfaces.

## Two-command source handoff

Assuming sibling private/public clones:

```powershell
# In WinInspect-private, from a clean committed source tree
pwsh ./scripts/New-PublicSourceProjection.ps1 `
  -OutputRoot ../WinInspect-projection

# In public WinInspect
pwsh ./scripts/Import-ProjectedSource.ps1 `
  -ProjectionRoot ../WinInspect-projection
```

Then review the public diff and commit only the generated `source/` and `.projection/` changes on a projection branch. Public CI performs the expensive build/test work.

The generated projection contains no private Git history. The public manifest records the exact private source commit and hashes every projected file. The public importer validates the projection before modifying the checkout, refuses an unexpected Git remote or dirty projection state, and restores the previous clean projection if final validation fails.

## While private hosted Actions are unavailable

Allowed:

- private product development and local source projection from clean immutable commits;
- public WinInspect builds, tests, packaging and release-readiness work;
- public Foundry projection/client work;
- local private Foundry validator execution against an exact private commit;
- preparation of private PRs whose correctness can later be replayed on a private or self-hosted runner.

Not allowed:

- treating a public build or lifecycle pass as private promotion approval;
- copying specialized private evaluator/formal logic into a public repository merely to obtain free CI;
- exposing private Git history, agent instructions, workflow credentials, or authority state through the product projection;
- weakening required private checks so a PR can merge;
- rewriting an immutable release to avoid a failed gate.

## Local private-validation evidence

When a private Foundry validator is run locally, preserve at minimum:

- private repository commit SHA;
- validator entrypoint/version;
- public release repository/tag/source SHA;
- exact artifact hashes;
- public lifecycle run identifiers/evidence digests being evaluated;
- UTC timestamp;
- platform/runtime information;
- pass/fail result for every private check.

The evidence should be replayable later with the same inputs. A later hosted/self-hosted run should confirm the result before relying on it as durable CI evidence.

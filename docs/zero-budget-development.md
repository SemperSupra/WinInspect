# Zero-budget development workflow

This repository is public, so standard GitHub-hosted Actions used here do not consume the private Foundry's monthly Actions allowance. Use that separation deliberately.

## Normal development loop

1. Make product changes in the authoritative private WinInspect development repository.
2. Run private/product-specific validation locally or on approved private execution capacity.
3. Generate a constructive sanitized source projection from an exact private source commit.
4. Import that projection into this public build/deploy repository on a review branch.
5. Run public projection validation, build/test/package/lifecycle workflows here.
6. Publish only immutable release subjects that passed product-owned public release gates.
7. Let public Windows Package Foundry exercise package-manager-specific client lifecycles against those exact subjects.
8. Let the private Foundry make the eligibility/promotion decision from immutable public evidence.
9. Project only the sanitized approved package model back into the public Foundry.

## While private hosted Actions are unavailable

Allowed:

- public WinInspect projection/build/test/packaging and release-readiness work;
- public Foundry distribution/client work;
- local private validator execution against an exact private commit;
- preparation of private PRs whose correctness can later be replayed on a private or self-hosted runner.

Not allowed:

- treating a public lifecycle pass as private promotion approval;
- copying or relocating specialized private tests, formal/evaluator logic, conformance fixtures, failure knowledge, or other private validation value into public files merely to obtain free CI;
- making a private repository public to gain hosted minutes;
- weakening required private checks so a PR can merge;
- rewriting an immutable release to avoid a failed gate;
- using direct pushes to public `main` as the normal generated-source publication path.

## Projection evidence

Every authoritative source projection should preserve at minimum:

- exact private source commit SHA;
- projection policy version/identity;
- emitted file count;
- per-file hashes and byte counts;
- canonical projection digest;
- authoritative/clean-tree status;
- public projection commit/PR and resulting public CI run identifiers.

A projection/build pass proves that the selected public source can be reconstructed and built. It is not evidence that the public tree contains the complete private validation system, and it must not be used to justify disclosing that system.

## Local private-validation evidence

When a private validator is run locally, preserve at minimum:

- private repository commit SHA;
- validator entrypoint/version;
- public release repository/tag/deploy SHA;
- authoritative private source SHA/projection digest when projected-source architecture applies;
- exact artifact hashes;
- public lifecycle run identifiers/evidence digests being evaluated;
- UTC timestamp;
- platform/runtime information;
- pass/fail result for every private check.

The evidence should be replayable later with the same inputs. A later hosted/self-hosted run should confirm the result before relying on it as durable CI evidence.

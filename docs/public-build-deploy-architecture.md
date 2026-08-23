# WinInspect private-development / public-build-deploy architecture

## Responsibility split

| Plane | Repository | Authority |
| --- | --- | --- |
| Private product development | `SemperSupra/WinInspect-private` | Product source, comprehensive tests, formal/conformance/evaluator material, internal development knowledge |
| Public product build/deploy | `SemperSupra/WinInspect` | Sanitized projected source, public build/test/package/release execution and provenance |
| Public distribution | `SemperSupra/windows-package-foundry` | Generic package projection and native-client lifecycle mechanics |
| Private promotion | `SemperSupra/windows-package-foundry-private` | Specialized eligibility, policy and promotion authority |

## Source projection contract

The public product repository is not a mirror of the private development repository. Product source is exported constructively from an exact private commit according to a fail-closed classification policy.

The canonical projection design should satisfy all of these invariants:

1. Every tracked private path is classified exactly once as public, private, or review-required; unclassified or multiply classified paths block export.
2. Only public-classified Git blobs from the exact recorded private source commit are emitted.
3. Comprehensive private tests, formal/conformance fixtures, evaluator corpora, internal research/failure knowledge and agent-development context remain private unless deliberately declassified.
4. The projection receipt records the exact private source SHA and hashes/size of every emitted file plus a canonical projection digest.
5. The public validator recomputes the receipt over `source/` before build or release.
6. Generated public source is replaced constructively, so files removed from the approved projection cannot survive as stale public source.
7. Public repositories never receive credentials capable of reading the private repositories.

## Validation split

Private development may run a substantially richer comprehensive test/evaluator suite. Public CI should contain enough public-safe checks to prove that the projected source builds, packages and behaves at the release/distribution interfaces without requiring publication of the private exam.

A failure caused by a private-only fixture is not solved by embedding or relocating that fixture into an allowlisted public source file. The correct fix is to preserve the private test graph and give the public plane a deliberately public-safe validation surface.

## Publication discipline

Projection updates should enter this repository through reviewable pull requests. Public `main` should be protected against direct generated-source pushes and force/rewrite operations, and the projection/deploy-plane check should be required before merge.

A release workflow consumes a reviewed public commit, validates its projection receipt, builds/packages the projected source and produces provenance/attestations. Public success is evidence for the downstream private authority; it is not itself promotion approval.

## Zero-budget operation

The architecture deliberately spends hosted execution in public repositories where standard GitHub-hosted Actions are available without consuming the private monthly Actions allowance. Private work can be validated locally and retained as replayable evidence until private hosted/self-hosted confirmation is available.

Cost pressure must not be used to move private evaluator value into public repositories or weaken promotion gates.

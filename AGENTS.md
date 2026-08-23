# AGENTS.md (WinInspect public deploy/release repository)

## Repository role

This repository is a **sanitized deployment/release plane**, not the authoritative development repository.

### Required behavior

- Do not implement normal product features or defect fixes directly here.
- Do not reconstruct or add private development context, comprehensive test corpora, formal/evaluator assets, internal audits, research, failure knowledge, agent-development guidance, or other value-bearing material.
- Do not add credentials or tokens capable of reading the private development repository or its Actions/artifacts/issues.
- Product-source changes must arrive through the approved constructive projection process from an immutable private candidate.
- Generated/projected source must not be hand-edited when the private source/exporter is authoritative.
- Public CI may build, run ordinary public-safe tests, package, generate SBOM/checksums/provenance/attestations, and stage release candidates.
- Public CI/provenance is not comprehensive correctness/security authority.
- Any emergency direct fix requires an explicit governance exception and must be reconciled into private development authority before the next projection.

## Value boundary

Public content should be limited to material deliberately classified as public-required/public-trust or an approved sanitized derivative.

The public repository should contain enough source, build material, documentation, and ordinary public-safe tests to make released versions inspectable and credible without publishing the comprehensive private exam.

Comprehensive private tests, formal/conformance fixtures, evaluator corpora, failure fingerprints, and equivalent validation knowledge must not be relocated into otherwise-public files merely to satisfy the projection boundary.

## Projection discipline

- Every generated source projection must carry an auditable receipt binding it to the exact authoritative private source commit and emitted-file hashes.
- Projection updates should arrive through a reviewable pull request and pass the public projection/deploy-plane checks before reaching `main`.
- Do not push generated source directly to `main` as the normal publication path.
- A successful public build proves public buildability of the projected source; it does not grant private promotion approval.

## Release discipline

A release candidate must be bound to the exact reviewed public commit and exact built artifacts. Where private specialized validation applies, promotion should publish the exact validated artifacts rather than rebuild after approval when practical.

## Visibility

Repository visibility is a release/governance decision. Agents must not change visibility, publish a release, or promote packages merely because source has been projected here.

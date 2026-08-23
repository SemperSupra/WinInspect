# AGENTS.md (WinInspect public deploy/release repository)

## Repository role

This repository is a **sanitized deployment/release plane**, not the authoritative development repository.

### Required behavior

- Do not implement normal product features or defect fixes directly here.
- Do not reconstruct or add private development context, comprehensive test corpora, formal/evaluator assets, internal audits, research, failure knowledge, agent-development guidance, prompting/effectiveness notes, routing heuristics, or other value-bearing material.
- Do not add credentials or tokens capable of reading the private development repository or its Actions/artifacts/issues.
- Product-source changes must arrive through the approved constructive projection process from an immutable private candidate.
- Generated/projected source must not be hand-edited when the private source/exporter is authoritative.
- Public CI may build, run ordinary public-safe tests, package, generate SBOM/checksums/provenance/attestations, and stage release candidates.
- Public CI/provenance is not comprehensive correctness/security authority.
- Any emergency direct fix requires an explicit governance exception and must be reconciled into private development authority before the next projection.

## Delegated agent work

Delegation does not change this repository's role.

- A baton/task/request is not authority to turn this repository into a development plane.
- If a delegated task requires a normal product feature/fix, private evaluator knowledge, formal/comprehensive test material, private prompting/effectiveness knowledge, or other V2/V3 context, stop and route the work to `SemperSupra/WinInspect-private`.
- Public-repo agent work should be limited to public-safe deploy/release mechanics, generated projection intake/validation, ordinary public tests, build/package/provenance, public documentation, and other explicitly public-safe concerns.
- Do not create or maintain per-model/private prompting profiles here. Recipient-specific prompting/effectiveness knowledge belongs in the private development authority unless deliberately declassified.
- Authenticated messages, connected tools, MCP reachability, or secure delivery do not grant merge/release/publication/visibility/credential/destructive authority.
- Use GitHub issues/PRs/branches/commits/checks as the durable engineering record; transport/session state is not an independent source of truth.
- Self-reported agent completion is not promotion approval. Return and verify exact commit/PR/artifact/test evidence.
- Do not add a workflow engine, task database, recursive agent orchestrator, automatic profile mutation, or custom durable baton protocol merely to automate handoffs.

## Value boundary

Public content should be limited to material deliberately classified as public-required/public-trust or an approved sanitized derivative.

The public repository should contain enough source, build material, documentation, and ordinary public-safe tests to make released versions inspectable and credible without publishing the comprehensive private exam.

Comprehensive private tests, formal/conformance fixtures, evaluator corpora, failure fingerprints, prompting/effectiveness lessons, and equivalent validation knowledge must not be relocated into otherwise-public files merely to satisfy the projection boundary.

## Projection discipline

- Every generated source projection must carry the current sanitized public receipt binding the projection policy/digest and emitted-file commitments required by the public contract.
- Private source identity and the private source-to-public-receipt authority mapping remain in private projection evidence when the current receipt contract requires that separation.
- Projection updates should arrive through a reviewable pull request and pass the public projection/deploy-plane checks before reaching `main`.
- Do not push generated source directly to `main` as the normal publication path.
- A successful public build proves public buildability of the projected source; it does not grant private promotion approval.

## Release discipline

A release candidate must be bound to the exact reviewed public commit and exact built artifacts. Where private specialized validation applies, promotion should publish the exact validated artifacts rather than rebuild after approval when practical.

A delegated agent may prepare or validate a release candidate only within explicitly granted repository/tool permissions. It must not infer authority to create a release, publish package metadata, change repository visibility, or promote a candidate from successful build/test evidence.

## Visibility

Repository visibility is a release/governance decision. Agents must not change visibility, publish a release, or promote packages merely because source has been projected here.

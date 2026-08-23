# Contributing to WinInspect

This repository is the public build/deployment/release plane for WinInspect. It is not the normal development repository.

## Bug reports and feature requests

Public issues are welcome. Describe the observed behavior, environment, reproduction steps, and desired outcome using only information you are comfortable making public.

Reports are triaged into the authoritative private development workflow. A resulting product fix is implemented and validated there, then reaches this repository through the reviewed sanitized source projection.

## Pull requests

Do not open ordinary feature or defect-fix pull requests directly against projected product source. Direct edits would create divergence from the authoritative private development state and cannot become the next release source without reconciliation.

Pull requests are appropriate for material that is explicitly authoritative in this public deployment plane, such as public-only release/build infrastructure, projection validators/import mechanics, deployment documentation, or generated projection updates produced by the approved airlock.

Generated projection updates should be reviewable pull requests and should pass the public deploy-plane/projection checks before merge. Direct pushes of generated source to `main` are not the normal publication path.

An emergency direct public fix requires an explicit governance exception and reconciliation back into private development authority before the next projection.

## Security reports

Follow `SECURITY.md` when present in a projected release. Do not publish credentials, exploit details, private data, or sensitive diagnostics in a public issue.

## Why the split exists

Released source remains inspectable and public builds remain observable, while comprehensive internal validation assets, failure knowledge, formal/conformance fixtures, research, and other value-bearing development material remain in the private development plane. Public build provenance is evidence of what was built from which projected source; it is not a claim that the public repository contains the complete internal validation system.

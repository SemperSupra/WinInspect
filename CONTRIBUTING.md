# Contributing to WinInspect

This repository is the public deployment/release plane for WinInspect. It is not the normal development repository.

## Bug reports and feature requests

Public issues are welcome when enabled. Describe the observed behavior, environment, reproduction steps, and desired outcome using only information you are comfortable making public.

Reports are triaged into the authoritative private development workflow. A resulting fix is implemented and validated there, then reaches this repository through the sanitized release projection.

## Pull requests

Do not open ordinary feature or defect-fix pull requests directly against projected product source. Direct edits would create divergence from the authoritative private development state and cannot become the next release source without reconciliation.

Pull requests may be appropriate for material that is explicitly authoritative in this public deployment plane, such as a reviewed correction to public-only release documentation or deployment infrastructure. When uncertain, open an issue first.

An emergency direct public fix requires an explicit governance exception and reconciliation back into private development authority before the next projection.

## Security reports

Follow `SECURITY.md` once it is present in a projected release. Do not publish credentials, exploit details, private data, or sensitive diagnostics in a public issue.

## Why the split exists

Released source remains inspectable and public builds remain observable, while comprehensive internal validation assets, failure knowledge, research, and other value-bearing development material remain in the private development plane. Public build provenance is evidence of what was built from which public source; it is not a claim that the public repository contains the complete internal validation system.

# WinInspect

This repository is the **public build/deployment/release plane** for WinInspect.

Normal product development does **not** occur here. The authoritative development repository is private. Product source reaches this repository only through the approved constructive sanitization/projection process.

The generated product tree lives under `source/`. Its receipt is `.projection/source-manifest.json`; public validation must bind the projected files to that receipt before build/release work proceeds.

Public CI may validate the projection, build and package the projected source, produce public-safe lifecycle evidence, and generate provenance. A successful public build or release is evidence, not private promotion approval.

Do not implement ordinary product fixes directly here. Do not add credentials capable of reading the private development repository. Do not relocate comprehensive private tests, formal/conformance fixtures, evaluator material, or equivalent private validation knowledge into public files merely to make the projection build.

Projection updates should be reviewed through pull requests and should pass the public projection/deploy-plane checks before reaching `main`.

# WinInspect

This repository is the **public build/deployment/release plane** for WinInspect.

Normal product development does **not** occur here. The authoritative development repository is private. Product source reaches this repository only through the approved constructive sanitization/projection process.

The repository is intentionally public so generic build, test, packaging, provenance, and release work can run on standard public GitHub-hosted runners without consuming the private repository's Actions allowance.

Until the first sanitized source projection is reviewed, this repository remains a public staging/deployment shell. The expected generated boundary is:

```text
.projection/source-manifest.json
source/...
```

Public CI verifies the manifest and every projected file before building. A public build success is execution evidence only; it does not grant private promotion approval.

Do not implement product fixes directly here. Do not add credentials capable of reading the private development repository. Do not copy private workflows, evaluator/formal material, agent instructions, or authority state into this repository merely to obtain public CI execution.

See `docs/public-build-deploy-architecture.md` and `docs/zero-budget-development.md` for the responsibility split and operating model.

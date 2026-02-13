# Agent Prompt: Bring up WinInspect through the full WBAB lifecycle

You are an implementation agent. Your mission is to bring up, implement, and test WinInspect using the WineBotAppBuilder (WBAB) lifecycle to target Windows and Wine environments.

Repository: WinInspect

## Constraints
- Core must be usable concurrently by CLI/GUI/API without interference.
- Behavior must be idempotent regardless of activation path.
- UI/CLI/API specialized behavior lives outside core.
- Formal models in TLA+ must be checked, and tests must validate model traces.
- Use WBAB pull-first runners; do not build locally unless explicitly enabled.

## Work plan (execute in order)
1. Replace placeholder WBAB integration:
   - Add WBAB as a submodule at `tools/WineBotAppBuilder` using `scripts/bootstrap-wbab-submodule.sh`.
   - Update `tools/winbuild-build.sh`, `tools/package-nsis.sh`, `tools/sign-dev.sh` to call the corresponding WBAB runners.
   - Ensure `tools/wbab` delegates cleanly to WBAB for build/package/sign/test/e2e.

2. Contracts:
   - Lock in `protocol/schema_v1.json` and `docs/PROTOCOL.md` as the API contract.
   - Add contract tests under `tests/contract/` validating CLI verbs, env vars, and protocol compliance.

3. Core:
   - Keep `core/` pure: deterministic + thread-safe. Extend snapshot semantics if needed.
   - Expand idempotent ensure actions (never toggle).
   - Strengthen canonical JSON to full JCS (RFC 8785) if required.

4. Daemon:
   - Implement multi-client named-pipe server with per-connection subscription queues (`events.subscribe + events.poll`).
   - Append to `.wbab/audit-log.jsonl` for every operation.

5. CLI:
   - Implement full CLI surface and golden output tests.

6. GUI:
   - Implement TreeView/ListView Win32 shell, but keep logic in ViewModel with unit tests.
   - Add minimal UI automation smoke tests (optional).

7. Formal + Model-validated tests:
   - Expand `formal/tla/WinInspect_v1.tla` to include retry semantics and event subscription invariants.
   - Maintain `formal/traces/` and ensure trace replay tests cover the invariants.

8. Packaging + signing:
   - Create NSIS scripts for x86/x64 or unified packaging.
   - Produce evidence in `dist/` (checksums, manifests).
   - Sign artifacts via WBAB signing runner.

9. E2E:
   - Mocked e2e in CI (already scaffolded).
   - Add opt-in real e2e workflow_dispatch for Wine (32-bit and WoW64 prefixes).

## Definition of done
- `tools/wbab preflight/build/test/package/sign/e2e` complete and produce evidence artifacts.
- CI green on push and PR.

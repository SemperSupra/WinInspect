# AGENTS.md (WinInspect)

## Prime directives
1. **Core purity:** `core/` must remain deterministic and safe for concurrent CLI/GUI/API use.
2. **Idempotence:** inspection must be side-effect free; actions (if any) must be desired-state (`ensureX`) not toggles.
3. **No UI logic in core:** CLI formatting, GUI interaction loops, and API presentation live outside `core/`.
4. **Model + tests:** update `formal/tla/` and ensure tests replay traces in `formal/traces/`.

## Repo map
- `core/`              Core library + fake backend + unit tests.
- `daemon/`            Named-pipe broker (`wininspectd`).
- `clients/cli/`       CLI client (`wininspect`).
- `clients/gui/`       Win32 GUI shell + testable ViewModel.
- `protocol/`          JSON schema + protocol contract.
- `formal/`            TLA+ model(s) and trace vectors.
- `tools/`             WBAB lifecycle wrappers (`wbab`, runners).
- `scripts/`           Preflight, packaging/signing placeholders.

## WBAB lifecycle
Use `tools/wbab` to run lifecycle steps. For full WBAB integration, add WineBotAppBuilder as a submodule
(see `scripts/bootstrap-wbab-submodule.sh`).

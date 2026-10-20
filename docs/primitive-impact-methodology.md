# Supplier-neutral primitive characterization and impact RDTE

## Mission

The target is **the minimum sufficient, independently qualified composition of primitives that completes actual user/agent workflows**. WinInspect, Playwright, WinApp CLI, Windows MCP Server, W3C WebDriver, direct Windows APIs, Win32/UIA, and future candidates are suppliers, not favored products. Reusing or retiring a WinInspect capability can be a positive finding.

Authoritative DLE: [WinInspect-private #367](https://github.com/SemperSupra/WinInspect-private/issues/367), [#387](https://github.com/SemperSupra/WinInspect-private/issues/387), [#406](https://github.com/SemperSupra/WinInspect-private/issues/406). Public RDTE remains DRAFT and uses only the already approved public source projection; private revisions never cross this boundary by inference.

## Representation

Machine-readable [primitive-impact-catalog.json](../qualification/primitive-impact-catalog.json) maintains:
- **Implementation inventory**: 72 WinInspect dispatch names from current approved public `source/core/src/core.cpp`, all **source-confirmed only** unless separately qualified. API counts do not equal distinct useful capabilities.
- **Semantic primitive contracts**: shared identifiers that ignore tool names. Each has expected behavior, operating preconditions, a separately measurable oracle, and metric dimensions.
- **Provider/primitive claims**: exact pinned provider/version, evidence tier, success/failure/not-executed status, source or native receipt and scope. `SOURCE_CONFIRMED` must never become `PASS` automatically.
- **Workflow requirements**: alternative acceptable minimal primitive bundles (OR of AND-sets), applied environment and constraints, exact consumer task evidence and an explicitly *unproven* marginal-utility field until controlled experiments establish it.
- **Decision policy**: authorization, private-source custody, zero-dollar admissibility, credentials, privacy, target/lease constraints, independent oracle and teardown are hard gates. Only admitted, verified treatments may be ranked.

The primitive ontology deliberately groups implementations into the same contract (e.g., `desktop.uia.snapshot` can be supplied by WinInspect, direct UIA, WinApp CLI, FlaUI or Windows MCP). Conversely, different capabilities of one product remain separate (e.g., window identity versus UIA enumeration versus acting on a native dialog).

## How impact is measured

**Stage A — Discovery:** record candidate functions, target environments, provider versions, upstream code, license, dependencies, privilege requirements and advertised limits. Include negative capability claims and source-backed caveats; do not treat claims as run receipts.

**Stage B — Primitive qualification:** run an isolated synthetic positive control, a known negative, stale-target and duplicate-target refusals, a timeout, bounded resource and cleanup checks. Verify both *operation dispatch* and an **independent postcondition**. Compare mechanism-distinct observers where possible: direct UIA vs Win32 vs Playwright DOM, not merely wrappers over the same UIA provider.

**Stage C — Matched task treatment:** on an identical disposable fixture and initial state, execute:
1. Supported baseline with a native/app-specific tool alone.
2. The same task with one additional supplier primitive.
3. The same task with a substitution, not an additional layer (e.g., WinApp UIA instead of WinInspect UIA).
4. Removal/ablation of each optional primitive, while preserving hard gates.
5. Targeted pairwise combinations for suspected interactions (window identity + UIA, browser protocol + native dialog, UIA + input lease).

Use stable task seeds and randomly alternate treatment order when interference exists. Reset browser profiles, VM snapshots, fixture files, window state and actor lease between treatments. Repeat across hosts after a positive lab result. On partial refusals or missing supported capabilities, record `BLOCKED` / `UNSUPPORTED` rather than a fabricated failure or win.

**Stage D — Contribution and interaction:** calculate, only from enough paired repetitions, the change in **independent task-success probability** when primitive `p` is added or removed, together with difference in p50/p95 time, human interventions, unsafe actions, failure recovery and maintenance cost. Attribute value to **sets** where primitives are complementary. A window-identity primitive can be worthless alone and indispensable to native UIA. A file-dialog primitive may be needless for Playwright-controlled HTML but necessary for a native application Save As.

Recommended primary metric: `delta_success(p | task, existing_set) = P(task_success | existing_set + p) - P(task_success | existing_set)`, backed by paired trials and confidence intervals. Add a survival/recovery metric for recoverable faults. Use sampled Shapley-style attribution **only** when interaction complexity and repetition support it; never infer causal credit from source inventory, three synthetic trials, or a single green CI run.

**Stage E — Portfolio composition:** represent workload goals as alternative acceptable primitive bundles; require all *hard gates* before comparing solutions. Search feasible combinations of providers plus adapters, accounting for shared failure domains (e.g., two libraries using the same Windows UIA provider are correlated), incompatible input ownership, platform support, installation/network constraints and credential boundaries. Compare a Pareto frontier on success, cost, risk, latency, footprint, maintenance and reuse. Choose `USE_NATIVE`, `USE_EXISTING_PROVIDER`, `COMPOSE`, `PRESERVE_EXISTING`, `BUILD_MINIMAL_WININSPECT`, `DEFER` or `RETIRE_REDUNDANT`. No WinInspect preference or unsupported weighted “best overall” number.

## First source-grounded findings (not causal impact estimates)

| Task | Supplier / mechanism | Independent observed evidence | Placement |
|---|---|---|---|
| HTML Unicode field and button | Playwright semantic DOM | Synthetic fixture PASS, stock Chrome/Edge | Browser DOM through Playwright |
| Stock Firefox field and button | W3C WebDriver + geckodriver | Synthetic fixture PASS on installed stock browser, not patched Playwright Firefox | Stock Firefox through WebDriver |
| HTML upload and download | Playwright filechooser + download | 3/3 synthetic uploads and 3/3 byte-hashed downloads | No additional WinInspect browser-file primitive |
| Native Edge window identity + immediate UIA | WinInspect + direct Windows UIA | Same visible HWND/PID, 2 children each; initial zero was wrong HWND selection | Preserve WinInspect current observation; improve target-selection contract |
| Native Edge inspected elements | WinApp CLI v0.7.1 | Same HWND, 3 elements reported at configured depth; different tree semantics | Competitor read-only proof, no dominance claim |
| Native UIA via Windows MCP v1.3.28 | Pinned vendor source and archive | Source-confirmed implementation, executable not yet trial-qualified | Pending exact-target MCP test |
| WinInspect framed RPC | WinInspect public projected daemon | Reply request ID mismatch independently reproduced | Transport remains failed; no action promotion |
| Real native file dialog | All providers | Not yet executed under approved input authority | Unknown, preserve gate |

**Important:** 3 WinApp elements versus 2 WinInspect *children* does **not** mean WinApp covers 50% more functionality. Normalization must compare equivalent tree views, selected depth, element identity and expected fixture elements.

## Feedback loop

Every RDTE receipt feeds `{workload, target, provider, primitives_used, initial_state, independent_oracle, observed_result, elapsed, interventions, risk_events, cleanup, evidence_uri}` into the catalog. Every missing capability must have a consumer and a falsifier. An implementation decision must cite the relevant *primitive-level* contrast and replay the original consumer task after change.

Minimum next experiments:
- Windows MCP's **allowlisted read-only** `ui_snapshot` on the same verified visible Edge HWND, compared with direct UIA, WinInspect and WinApp. Treat HTTP/stdio framing and tool permission checks as distinct transport/security primitives.
- Native Windows Open/Save dialog in disposable app under one admitted input lease, then compare only suppliers needed beyond Playwright/WebDriver.
- Stale HWND, occluded/hidden/multiple windows, DPI/locale and locked-session negative controls.
- Build/source changes only when experiments show a missing or clearly underserved primitive that merits maintenance.

### Maintainability

Run `node scripts/validate-primitive-impact.cjs` whenever the source inventory, claims, workflow rows or evidence tiers change. The catalog and validator themselves do not grant source publication, private runner use, vendor network egress, UI actuation, or merge authority.

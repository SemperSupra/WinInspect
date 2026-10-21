# PID-scoped independent window selection — WinApp CLI RDTE

**Evidence level:** `EXECUTED_ON_EXACT_TARGET`, **synthetic only**, not `CONSUMER_QUALIFIED`. Evidence collected on unchanged public projected-source Windows runner, no private repository inputs, personal profiles, credentials, remote sites or mutating desktop operation. 

## Exact authority

- Public draft [PR #40](https://github.com/SemperSupra/WinInspect/pull/40) source `1dd139135aabf0f604247141fcbf198b6d8be1b4`.
- Free Windows [Actions run 39677672742](https://github.com/SemperSupra/WinInspect/actions/runs/39677672742) **SUCCESS**, public security [39677673027](https://github.com/SemperSupra/WinInspect/actions/runs/39677673027) **SUCCESS**.
- [Archived independent receipt #12382453536](https://github.com/SemperSupra/WinInspect/actions/runs/39677672742/artifacts/12382453536), exact ZIP SHA-256 `3ec850400d31d042c0ba2b552b1749d5911c7d8a1f8303439831cdf45a33f1de` (GitHub artifact digest confirmed against downloaded bytes).
- Receipt `public-browser-rdte.json` has `schema=public-browser-rdte/v1`. Guarded WinApp CLI v0.7.1 supply as already admitted in original public CI; Windows MCP runtime **not admitted**.

## Matched same-lifetime synthetic target test

For each of ordinary Edge and Edge with forced accessibility, harness creates an isolated browser process and supplies its ephemeral PID (never publishes PID, HWND, window title or UI text). WinApp CLI invokes `ui list-windows -a <PID> --json` with telemetry/update disabled, process-restricted egress guard. It independently filters returned windows by **that PID**, `Chrome_WidgetWin` class, and width/height greater than 100; it refuses zero or multiple candidates instead of arbitrarily choosing one. Critically, the WinInspect-selected HWND is **NOT an input to this selector**.

Only *after* WinApp has chosen does a separate Win32 `GetWindowThreadProcessId` and `UIAutomationElement.FromHandle` oracle verify PID/root, followed by comparison with WinInspect's independently selected visible HWND. Existing WinInspect and direct Windows UIA child observations remain separate controls.

| Native Edge trial | Eligible WinApp candidates | Selected HWND PID & UIA root | Match to independently selected reference | Outcome |
| --- | ---: | --- | --- | --- |
| Normal | 1 | PASS / PASS | Yes | `SELECTED_ORACLE_MATCH` |
| Accessibility forced | 1 | PASS / PASS | Yes | `SELECTED_ORACLE_MATCH` |

In both trials `referenceUsedForSelection=false`, the WinApp class/size check passed, WinInspect and direct native UIA each found **two** child elements on the matched target. WinApp's separate depth-one inspect returned **three** elements, but provider tree semantics differ; numbers are **not a ranking**.

## Limitations and gates

This qualifies WinApp as one **interchangeable provider of the narrow `desktop.window.identify` primitive when the synthetic browser PID is supplied**, and strengthens confidence in its enumeration. It does **not** prove arbitrary host-wide window/process discovery, multiwindow ambiguity resolution, production GUI/WinBot task completion, native file-picker actuation, virtual machine/Wine parity, latency distribution, reliability at scale, or statistically causal provider advantage. The existing WinInspect named-pipe `idMatched=false` remains a **known failure**, not silently waived by Windows CI success. No actual window focus/input/mutation was performed. Preserve branch/review, public-source custody, budget, deployment and release gates. This record documents exact-source evidence only; it does not authorize merges or promotion.

# Public WinInspect + Playwright browser RDTE (candidate)

Authority: WinInspect-private #387 / #406. Public source only from already approved schema-v2 projection; no private source injection.

## Scope and tiers

A. Stock browser inventory: Edge, Chrome, Firefox, Opera; optionally Brave and Vivaldi. Record installed and version separately. Do not install unsupported third-party browsers automatically or access existing profiles.
B. Playwright semantic DOM: synthetic offline fixture, Unicode form fill, button click, independent resulting DOM readback, synthetic download verified by SHA-256; isolated disposable context/profile. Browser-specific caveat: Playwright's bundled Firefox is a patched Playwright engine and is NOT a stock Firefox acceptance. Stock Firefox and Opera require separately measured compatibility, not assumed support.
C. WinInspect observation: read-only isolated loopback/named-pipe daemon, window.listTop, window.getInfo matching browser process PID, scoped ui.inspect. Never invoke mouse/key, credential or clipboard operations in initial public CI.
D. Cross-tool pairing: prove both Playwright DOM action and WinInspect observation in the same target browser process/session. Headless DOM success is not evidence of visible desktop UIA. Hosted noninteractive sessions get explicit INCONCLUSIVE.
E. Later actuator gate: WinBot/Agent Dispatch owns one desktop input lease, planning, actions, admission, retry and independent oracle; native WinInspect only when browser DOM / supported browser mechanisms cannot perform an operation.

## Controlled failure cases

- Missing browser: NOT_INSTALLED (not PASS).
- Unsupported stock browser protocol or CDP: UNSUPPORTED.
- No interactive desktop or window: NONINTERACTIVE.
- Recycled/unmatched HWND/PID: STALE_TARGET, never guess.
- RPC timeout, truncated/overlength frame: TRANSPORT_FAILURE.
- Playwright DOM vs native-window disagreement: CROSS_TOOL_MISMATCH.
- Download creation without matching independent hash: POSTCONDITION_FAILURE.
- Core response ID mismatch: KNOWN_PRODUCT_DEFECT pending authoritative corrective PR, never ignore in release acceptance.

## Existing native evidence and blocker

Public WinInspect PR #34 on the approved projected baseline built and ran actual native binaries; the read-only named-pipe health reply is readable but response-ID correlation fails. Private authoritative draft PR #415 contains a source correction and regression tests but is not approved for public declassification or release. The public browser RDTE must not silently pull that private source into public CI. Current tests are proposed; no browser RDTE CI receipt exists yet.

## Acceptance and funding boundaries

Use a public GitHub-hosted Windows runner only, no private Actions minutes; no secrets, real accounts, external websites, production profiles, privileged installers, user desktop, or production deployment. Publish exact source commit and binary SHA, browser versions, result matrix, classification, and bounded cleanup evidence. A successful DOM test is independent of native UIA; a native UIA observation is independent of actual click/type reliability. No merge/release by inference.

## Next action

Qualify and publish the prepared public-only workflow and test probe through an admitted repository-write path; run public Actions at exact test head; independently inspect terminal job steps and sanitized artifact; repeat failed cases only. Keep issue #387 authoritative and #406 as executable delta ledger.
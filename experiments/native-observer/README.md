# Synthetic native WinEvent feasibility workcell

Standalone Win32 source-bound probe on free public Windows and Linux Wine/Xvfb GitHub Actions. This is independently authored, synthetic test-only code. It does not import or copy private WinInspect product code.

The probe launches exactly one child copy of itself and creates/shows/destroys a synthetic window. An out-of-context event hook with WINEVENT_SKIPOWNPROCESS counts only cross-process object-create/show events owned by that child. No real application, HWND, PID, title, user files, screenshots, clipboard, input or credentials are recorded. Child, hooks and disposable Wine prefix are cleaned up.

Exit 0 = required synthetic event delivery observed; 2 = hook registration unavailable; 3 = required delivery absent; 4 = synthetic process/environment failed; 5 = invalid invocation. A workflow may succeed at recording code 2/3/4, but this is explicitly NOT an observer delivery PASS.

This determines whether a runner is eligible for later exact-product-native testing. A follow-up must build the validated v0.5 source projection, run WinInspect framed observer RPC, check read-only denial, cursor/overflow/restart and independent snapshot readback. No release acceptance follows from this experiment alone.

Authority: SemperSupra/WinInspect-private #367, #387, #406 and #463.

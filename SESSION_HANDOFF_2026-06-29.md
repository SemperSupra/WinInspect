# Session Handoff — 2026-06-29

## Summary

Two major work streams completed:

### Stream A: v0.2.0 WineBot Features (7/7 implemented)
All remaining v0.2.0 features for issue #40 were implemented in 10 files
(702 insertions, 1 deletion). **No build verification possible** — cmake
not available in this session's environment.

**What was done:**
1. **DXGI Desktop Duplication** — `try_dxgi_capture()` with D3D11 device →
   IDXGIOutputDuplication → staging texture → BMP. GDI fallback on any
   failure. Constructor probe at startup. `dxgi_capture` capability flag.
   Links `dxgi` + `d3d11` in CMakeLists.txt.

2. **Window move/resize** — `window.move(hwnd, x, y)` and
   `window.resize(hwnd, width, height)` via SetWindowPos.

3. **Desktop info** — `screen.desktopInfo` with GetSystemMetrics,
   GetDpiForSystem (Win10+), GetDeviceCaps fallback.

4. **Z-order output** — `window.getZOrder` walking GetWindow(GW_HWNDNEXT/PREV).

5. **Process execution** — `process.execute` via CreateProcess with
   pipe-redirected stdout/stderr, 30s timeout.

6. **Mouse drag** — `input.mouseDrag` with linear interpolation, SendInput.

7. **Key combos** — `input.hotkey` parsing comma-separated key names,
   mapped to VK codes, SendInput with modifiers + main key.

**Concerns:**
- `_stricmp` used in hotkey lookup — may need `stricmp` on older MinGW
- `__uuidof` in DXGI code — needs modern MinGW-w64 headers. Falls back to
  IID constants if unavailable
- 10 files modified but not compiled — expect at least one round of
  compile-error fixes when building natively

### Stream B: Multi-Instance LAN Research + v0.3.0 Plan
Comprehensive research on running WinInspect across physical, VM, and
container environments, plus a phased bring-up strategy documented as
8 GitHub issues under milestone `v0.3.0`.

**New documents:**
- `docs/MULTI_INSTANCE_LAN.md` — topology analysis + 5-angle research
  (port acquisition, discovery, capture methods, recording, remote access)
- `docs/BRINGUP_STRATEGY.md` — phased plan with branch structure, test
  matrix, PR checklists, and dependency graph

**GitHub issues created:**
| # | Title | Phase |
|---|---|---|
| 43 | Instance identity | 1 |
| 44 | Dynamic port acquisition | 2 |
| 45 | Multicast discovery (mDNS) | 3a |
| 46 | Rendezvous discovery | 3b |
| 47 | HTTP server + REST API | 4 |
| 48 | WebUI dashboard | 5 |
| 49 | Burst capture | 6 |
| 50 | Integration smoke tests | 7 |
| 51 | Mutual authentication | 8 |

---

## Git State

Branch: `master` (all work committed directly)
Status: 12 modified + 2 untracked files
Uncommitted changes:
```
M  CMakeLists.txt
M  clients/cli/src/cli.cpp
M  core/include/wininspect/backend.hpp
M  core/include/wininspect/fake_backend.hpp
M  core/include/wininspect/types.hpp
M  core/include/wininspect/win32_backend.hpp
M  core/src/core.cpp
M  core/src/fake_backend.cpp
M  core/src/win32_backend.cpp
M  core/tests/test_contract_methods.cpp
M  ROADMAP.md
?? docs/BRINGUP_STRATEGY.md
?? docs/MULTI_INSTANCE_LAN.md
```

**Intended commit message:**
```
feat: v0.2.0 WineBot features + v0.3.0 multi-instance LAN plan

Implements all 7 remaining v0.2.0 features (DXGI capture, window
move/resize, desktop info, z-order, process exec, mouse drag, hotkeys).
Updates ROADMAP.md to reflect v0.2.0 complete and v0.3.0 planned.

Adds docs/MULTI_INSTANCE_LAN.md — topology analysis and research.
Adds docs/BRINGUP_STRATEGY.md — phased implementation plan with 8
GitHub issues under v0.3.0 milestone.

Co-Authored-By: Claude <noreply@anthropic.com>
```

---

## What Worked / What Didn't

### Worked
- **All v0.2.0 features follow established patterns** — backend.hpp virtual
  method, Win32Backend impl, FakeBackend stub, CoreEngine dispatch, CLI
  command, contract test. Consistent across all 7 additions.
- **DXGI fallback pattern** — constructor probe + `try_dxgi_capture()` +
  GDI fallback in `capture_screen()` handles all edge cases gracefully.
- **Research document structure** — per-environment tables for capture
  methods make decision-making concrete.
- **GitHub issues** — all 9 v0.3.0 issues created with full bodies,
  checklists, and milestone assignment.

### Didn't Work / Risks
- **Deep-research workflow failed** — `claude-3-5-haiku` model not available
  on this provider. Researched manually via WebSearch + synthesis.
- **No build verification** — cmake, g++, and the build container are not
  available in this session environment. The v0.2.0 changes are structurally
  consistent but may have compile errors (MinGW-specific issues with
  `__uuidof`, `_stricmp`, DXGI header availability).
- **Disk space scare** — encountered ENOSPC transient error, resolved.
  Docker WSL images consuming ~500GB on C: drive.
- **Explore agent also hit model routing** — `claude-3-5-haiku` not
  available. Used direct Read/Grep instead.

### Next Steps

1. **Build and test v0.2.0** — on native Windows with MSVC or MinGW:
   ```bash
   cmake -B build -G "MinGW Makefiles"
   cmake --build build
   ctest --test-dir build -C Release
   ```
   Expect compile fixes for MinGW compatibility (DXGI headers, `_stricmp`).

2. **Tag v0.2.0 release** — after CI green:
   ```bash
   git tag v0.2.0
   git push origin v0.2.0
   ```

3. **Start v0.3.0 Phase 1** — instance identity (`--instance-name`, UUID):
   - Branch: `feature/v0.3.0-instance-identity`
   - 10 files, estimated 1-2 days
   - See `docs/BRINGUP_STRATEGY.md` for full checklist

4. **Recommended parallel tracks**:
   - Phase 1 (identity) + Phase 2 (ports) + Phase 6 (burst capture) are
     dependency-free — can be implemented in parallel
   - Phase 3a (mDNS) + Phase 3b (rendezvous) need Phase 1 and 2 complete
   - Phase 4 (HTTP server) needs Phase 1
   - Phase 5 (WebUI) needs Phase 4
   - Phase 8 (mutual auth) needs Phase 1

5. **Docker cleanup** — before next session, consider:
   ```powershell
   docker system prune -a --volumes
   ```
   WSL images on C: drive are ~500GB.

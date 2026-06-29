# Session Handoff — 2026-06-28/29

## Session Summary

Triaged 30 open GitHub issues to 13, implemented `--no-clipboard` flag,
discovered local WBAB container build pipeline, verified all tests pass.

---

## What Worked

### 1. WBAB Build Container (Major Discovery)
The Microsoft/MinGW cross-compile container is already pulled locally:
`ghcr.io/sempersupra/winebotappbuilder-winbuild:v0.3.7` (4.36 GB)

Build command:
```bash
MSYS2_ARG_CONV_EXCL="*" docker run --rm \
  -v "C:\Users\Mark\projects\WinInspect:/workspace" \
  -w /workspace \
  ghcr.io/sempersupra/winebotappbuilder-winbuild:v0.3.7 \
  bash -c 'cmake -S . -B build -DWININSPECT_BUILD_TESTS=ON &&
           cmake --build build &&
           cd build &&
           cmake -S .. -B . -DWININSPECT_BUILD_TESTS=ON &&
           cmake --build .'
```

- CMake 3.31.6, g++ 14-win32 (MinGW)
- Produces 6 executables (3 production + 3 test)
- 65/65 tests pass via Wine

### 2. Issue Triage
- **18 of 30 issues** closed (60% reduction)
- 5 closed as already implemented (#10, #33, #35, #37, #39)
- 8 triaged to v0.2.0 roadmap epic (#28-#38)
- 3 legacy issues closed (#7, #8, #9)
- 1 implemented this session (#24 — `--no-clipboard`)
- 13 remaining across code/architectural/CI buckets

### 3. --no-clipboard Implementation (#24)
- Adds `--no-clipboard` daemon flag
- Returns `E_ACCESS_DENIED` for clipboard.read and clipboard.write
- Verified compiled in WBAB container — strings check shows the error message
- Wire pattern matches --admin-logs and --require-auth
- Note: Pipe handler only. TCP handler needs same wiring.

### 4. Build Verification
| Check | Result |
|---|---|
| Compilation | 6/6 executables, zero warnings |
| test_core | 65/65 PASSED |
| test_discovery | PASSED |

---

## What Didn't Work

### 1. TCP Handler --no-clipboard Wiring
The `handle_socket_client` and `TcpServer::start()` signatures need an additional
`bool no_clipboard` parameter. The clipboard intercept code was added to
`tcp_server.cpp` but references an undeclared variable — backed out. Wire before
next release.

### 2. GitHub Actions CI Minutes Exhausted
All free-tier minutes consumed. No CI runs possible until minutes reset or
additional credits obtained. WBAB container is the local build alternative.

### 3. Bash `sed` for Multiline Replacements
Several shell `sed` commands produced corrupted files. Recovered with `git checkout`.
PowerShell's string.Replace() approach worked better for multiline insertions.

---

## State of the Project

| Metric | Value |
|---|---|
| Branch | `master` — clean, up to date |
| Release | v0.1.1 (published) |
| Open Issues | 13 (from 30 — 57% reduction) |
| CI | All `master` runs passing (last: success) |
| Build Pipeline | WBAB container available locally |
| Tests | 65/65 passing |

---

## Remaining Open Issues (13)

| Priority | # | Item | Est. Effort |
|---|---|---|---|
| **Code** | 15 | Connection rate limiting | Small |
| | 18 | shared_ptr<Snapshot> | Medium |
| | 19 | GetDIBits pixel search | Medium |
| **Doc** | 20 | Protocol versioning policy | Small |
| | 21 | JSON config file (--config) | Medium |
| | 22 | Granular mutex locks | Medium |
| **Arch** | 16 | Watchdog timeout cancellation | Medium-Hard |
| | 17 | Deduplicate pipe/TCP dispatch | Medium |
| | 23 | Method-level authorization | Medium |
| | 25 | Audit logging | Medium-Hard |
| **CI** | 26 | Native smoke test (blocked) | Small |
| | 27 | TCP fuzz tests (blocked) | Small |
| **Epic** | 40 | v0.2.0 WineBot features | Large |

---

## Next Session Quick Start

```bash
# Build and test
MSYS2_ARG_CONV_EXCL="*" docker run --rm \
  -v "C:\Users\Mark\projects\WinInspect:/workspace" \
  -w /workspace \
  ghcr.io/sempersupra/winebotappbuilder-winbuild:v0.3.7 \
  bash -c '
    export WINEPREFIX=$HOME/wp && mkdir -p $WP && wine wineboot --init 2>/dev/null
    cmake -S . -B build -DWININSPECT_BUILD_TESTS=ON
    cmake --build build -- -j$(nproc)
    cd /workspace && wine build/test_core.exe --no-colors | grep -v "^[0-9a-f]\{4\}"
  '
```

### Priority Actions
1. Wire TCP handler for `--no-clipboard` (add `bool no_clipboard` to TcpServer::start, handle_socket_client)
2. Tag v0.1.2 with bug fixes
3. Implement rate limiting (#15) and shared_ptr snapshots (#18)
4. Submit SignPath Foundation application
5. Add i386 arch to WBAB for test_gui_viewmodel Wine execution

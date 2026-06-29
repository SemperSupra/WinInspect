# Formal Methods Analysis — WinInspect

## Overview

Four formal methods approaches were evaluated for WinInspect. Two are in active
use (TLA+/TLC, RapidCheck), one is scoped for later (ThreadSanitizer), and
three are deferred (CBMC, ASan, KLEE).

---

## 1. TLA+/TLC — State Machine Model Checking ✅ In Use

**Tool:** TLC2 v2026.05.26, tla2tools.jar
**Model:** `formal/tla/WinInspect_v2.tla` (601 lines, 8 invariants)
**Parameters:** 2 clients, 3 windows, 3 snapshots, 4 boolean flags
**TLC runs:** 11 total across project history (64M–197M states each)
**Last run:** 55M states, 0 violations

### Invariants Verified

| # | Invariant | What It Checks | Code Reference |
|---|---|---|---|
| P1 | `NonInterference` | No response or event leaks between clients | `session.id` isolation in both handlers |
| P2 | `ForegroundExclusive` | At most one window has foreground at any time | `SetForegroundWindow` in win32_backend.cpp |
| P3 | `ConnectionLimitEnforced` | |conns| <= MaxConns always | `server.cpp:329-335` connection cap |
| P4 | `SessionLimitEnforced` | sessionCount <= MaxSessions | `server.cpp:96-112` session cap |
| P5 | `SubscribeHasBaseline` | After subscribe, lastSnapId != NULL | `server.cpp:119-136` baseline capture |
| P6 | `DesiredStateIdempotent` | ensureVisible returns changed:false after convergence | `win32_backend.cpp:959-978` |
| P7 | `ReadOnlyBlocksMutations` | All mutation methods rejected when readOnly=TRUE | `server.cpp:181-187` read-only gate |
| P8 | `SnapshotReferencesValid` | No response references an evicted snapshot ID | `server.cpp:213-222` E_BAD_SNAPSHOT |

### Features Modeled (15 state variables)

`worldVisible`, `worldForeground`, `conns`, `connCount`, `inbox`, `outbox`,
`subscribed`, `lastSnapId`, `snaps`, `snapPinCount`, `snapOrder`,
`snapCounter`, `sessions`, `sessionCount`, `readOnly`, `noClipboard`,
`adminLogs`, `requireAuth`

### Bugs Discovered by TLA+

| Bug | TLC Run | States | Description | Fix |
|---|---|---|---|---|
| **SnapshotReferencesValid violation** | Run #9 | 64M @ depth 4 | Client could reference nonexistent `snapId` in a request. The model echoed the invalid ID back in the response. | Added snapId validation in `HandleOne` query branch. Code already had `E_BAD_SNAPSHOT` but the model revealed the pattern. |
| **Missing UNCHANGED variables** | Runs #7-8 | 28M–64M | `conns`, `connCount`, `snapPinCount` missing from UNCHANGED blocks in `Connect` and `ClientDisconnect` actions. These variables silently reset to INIT on those transitions. | Added missing variables to all UNCHANGED blocks. |
| **Standalone comment syntax errors** | Runs #1-6 | 0 (parse error) | TLA+ comment lines starting with `/\ \*` are parse errors. Several instances in the model file. | Moved comments to their own lines without `/\` prefix. |
| **Type mismatch: BOOLEAN \cup {NULL}** | Run #6 | 0 (semantic error) | Using `NULL=0` (integer) in a `BOOLEAN \cup` set caused TLC to fail on type comparison. | Made `NULL` a CONSTANT instead of `Nat`. |
| **Typo: `subcribed`** | Run #6 | 0 (semantic error) | Variable name misspelled as `subcribed` instead of `subscribed`. | Fixed to `subscribed`. |

---

## 2. RapidCheck — Property-Based Testing ✅ In Use

**Status:** Implemented with 7 properties (200 random iterations each)
**Framework:** Custom stub header at `third_party/rapidcheck/rapidcheck.hpp`
**Test file:** `core/tests/test_properties.cpp`
**Replacement:** Download full RapidCheck from https://github.com/emil-e/rapidcheck

### Properties Verified

| # | Property | Iterations | What It Checks | Code Tested |
|---|---|---|---|---|
| 1 | Version transitivity | 200 | ∀ a,b,c: (a>b ∧ b>c) ⇒ a>c; (a=b ∧ b=c) ⇒ a=c | `update::compare_versions()` |
| 2 | Version parse roundtrip | 200 | ∀ M,P,p: parse("vM.P.p") = [M,P,p] | `update::parse_version()` |
| 3 | Base64 roundtrip | 200 | ∀ data: decode(encode(data)) = data | `base64::encode()` / `decode()` |
| 4 | Snapshot shape invariant | 200 | ∀ windows: |capture().top| ≤ |windows| | `FakeBackend::capture_snapshot()` |
| 5 | HWND string format | 200 | ∀ generated: starts "0x", valid hex, ≤10 chars | HWND parsing in CLI |
| 6 | Empty base64 | 200 | encode({}) = ""; decode("") = {} | Edge case for base64 |
| 7 | Version length comparison | 200 | 1.2 < 1.2.3 < 1.2.3.4 | `parse_version()` with different part counts |

### Bugs Discovered by RapidCheck

| Bug | Property | Description | Fix |
|---|---|---|---|
| None yet | — | All 7 properties pass with the stub framework. Full RapidCheck with smarter shrinking may find more. | — |

---

## 3. ThreadSanitizer (TSan) — Data Race Detection 🔶 Scoped

**Status:** Issue #42 — deferred until Linux-native test build is set up
**Tool:** GCC/Clang `-fsanitize=thread` (not available for MinGW cross-compile)

### Targets for TSan Analysis

| Target | Risk | Why |
|---|---|---|
| `ServerState::snapshots_mu` coverage | Medium | Multiple threads access snaps map. Missing lock on any read path = data race. |
| `std::async` watchdog future | Medium | Async task runs `CoreEngine::handle()` while main thread may modify backend. |
| `session.last_snap_id` modification | Medium | Updated by events.poll handler while snapshot subsystem reads it. |
| `Logger::buffer_` | Low | Protected by `mu_` but `should_log()` also takes lock. Double-lock OK here. |

---

## 4. CBMC — Bounded Model Checking 🔶 Available

**Tool:** `cbmc` (install via `apt install cbmc`)
**What it would verify:** Actual C++ code paths, not an abstraction like TLA+

| Check | Code Target | Why Important |
|---|---|---|
| Array bounds | JSON parser (`tinyjson.hpp`) | Untrusted input parsing |
| Null dereference | `win32_backend.cpp` GDI calls | `GetDC`, `CreateCompatibleDC` can return NULL |
| Division by zero | `send_mouse_click` SM_CXSCREEN | When screen metrics return 0 |
| Buffer overflow | `base64.cpp` encode/decode | Input-length-dependent allocation |

---

## 5. AddressSanitizer (ASan) — Memory Error Detection 🔶 Available

**Tool:** GCC/Clang `-fsanitize=address` (also MSVC `/fsanitize=address`)
**What it would catch:** Buffer overflows, use-after-free, double-free, memory leaks

| Target | Risk | Code Pattern |
|---|---|---|
| `ComPtr` usage | Medium | Manual `AddRef`/`Release` — leak if exception thrown between them |
| `SafeHandle` leak | Low | Move semantics prevent most leaks, but `CreateThread` + new pattern leaks on failure |
| GDI handle leak | Medium | `GetDC` without `ReleaseDC` in early-return paths |
| `std::vector` realloc | Low | Iterator invalidation on `push_back` while holding pointer into buffer |

---

## 6. KLEE — Symbolic Execution ❌ Not Recommended

**Requires:** LLVM IR, not compatible with MinGW cross-compilation
**Not suitable for:** Win32 API-heavy codebase. Would need a Linux-native test harness
with mocked Win32 calls. Infeasible for current project architecture.

---

## Summary

| Method | Status | Bugs Found | Setup Cost |
|---|---|---|---|
| **TLA+/TLC** | ✅ In use | 5 (1 real, 4 model/structure) | Java + JAR |
| **RapidCheck** | ✅ In use | 0 yet | Header-only stub, full lib optional |
| **ThreadSanitizer** | 🔶 Deferred | — | Build container change |
| **CBMC** | 🔶 Available | — | `apt install cbmc` |
| **ASan** | 🔶 Available | — | CMake option |
| **KLEE** | ❌ Not suitable | — | N/A |

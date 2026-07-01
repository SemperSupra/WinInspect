# Formal Methods Assessment — WinInspect

## Executive Summary

WinInspect already has a mature TLA+ model (V2, 601 lines, 8 invariants,
197M states explored) and property-based tests via RapidCheck. This puts it
ahead of most projects in formal-methods adoption. The question is not
"should we start?" but "what's next?"

This assessment evaluates the current state, identifies remaining risk areas,
and recommends a phased path forward.

---

## Stage 1 — Repository Assessment

### 1. Core Correctness Risks

| Risk | Severity | Location | Current Coverage |
|---|---|---|---|
| **Concurrent session isolation** | High | `tcp_server.cpp` — multiple client threads sharing ServerState | TLA+ NonInterference invariant (P1) |
| **Snapshot lifecycle (pin/evict)** | High | `tcp_server.cpp` lines 337-426 — LRU eviction with pin counts | TLA+ SnapshotReferencesValid (P8) |
| **Auth/encryption handshake** | Critical | `tcp_server.cpp` lines 104-193 — ECDH + nonce + SSH sig | Not modeled |
| **Custom crypto protocol** | Critical | `core/src/crypto.cpp` — ECDH/AES-GCM implementation | Not modeled |
| **JSON parsing** | Medium | `tinyjson.hpp` — no schema validation | Not fuzzed |
| **Rate limiting + DoS** | Medium | `tcp_server.cpp` lines 517-526 — global rate limit | Not modeled |
| **Process execution** | High | `win32_backend.cpp` — CreateProcess with pipe redirection | Not modeled |
| **Registry operations** | Medium | `win32_backend.cpp` — reg_read/reg_write | Not modeled |
| **IPC (named pipe)** | Medium | `pipe_win32.cpp` — pipe protocol | Not modeled |
| **DXGI capture** | Low | `win32_backend.cpp` — GPU memory management | Not modeled |
| **Config file I/O** | Low | `network_config.cpp` — file corruption handling | Not modeled |

### 2. Components with High State Complexity

| Component | State Variables | Concurrency | Modeled? |
|---|---|---|---|
| Snapshot LRU cache | snaps, snapOrder, snapPinCount, snapCounter | Multiple readers/writers | ✅ TLA+ |
| Session registry | sessions, sessionCount, subscribed, lastSnapId | Per-connection threads | ✅ TLA+ |
| Connection lifecycle | conns, connCount | Accept loop + client threads | ✅ TLA+ |
| **Auth handshake** | crypto state, nonce, auth_keys | Per-connection | ❌ Not modeled |
| **mDNS responder** | service registration state | Background thread | ❌ Not modeled |
| **Rendezvous client** | domain map, heartbeat threads | Background threads | ❌ Not modeled |
| **CIDR allow/deny** | allow_cidrs, deny_cidrs | Accept loop | ❌ Not modeled |

### 3. Protocols, State Machines, and Safety-Critical Logic

- **TCP handshake protocol** — 4-message exchange (hello → auth → key exchange → session)
- **Snapshot lifecycle** — capture → pin → unpin → evict (LRU with reference counting)
- **Session lifecycle** — create → subscribe → poll → unsubscribe → terminate
- **Event subscription** — subscribe → baseline capture → poll(diff) → unsubscribe
- **Desired-state idempotence** — ensureVisible/ensureForeground converge after 1st call
- **Auth/authorization** — require-auth gate, allow/deny lists, read-only mode
- **Rate limiting** — per-IP rate tracking with map
- **Auto-update** — periodic GitHub release check
- **Rendezvous registration** — register → heartbeat (30s) → deregister

### 4. Existing Tests and Gaps

| Test Suite | Count | Coverage | Gaps |
|---|---|---|---|
| `test_core` (contract) | 55+ | All protocol methods via FakeBackend | No negative tests for malformed input |
| `test_properties` (RapidCheck) | 8 properties | Version comparison, base64, snapshot shape | No auth/encryption property tests |
| `test_uia` | — | UI Automation element iteration | Minimal |
| `test_gui_viewmodel` | — | ViewModel bindings | UI-only |
| `test_discovery` | — | UDP broadcast | Single test |
| **No fuzz tests** | 0 | — | JSON parser, protocol parser |
| **No crypto tests** | 0 | — | Handshake, encryption roundtrip |

### 5. Areas Where Formal Methods Would Be Useful

| Area | Method | Priority |
|---|---|---|
| Auth handshake protocol | TLA+ model extension | High |
| Custom crypto (ECDH + AES-GCM) | TLA+ / symbolic model | High |
| Multi-instance rendezvous protocol | TLA+ | Medium |
| CIDR matching correctness | Property-based tests | Low (simple bit ops) |
| JSON parser robustness | Fuzzing | Medium |
| Thread safety (data races) | ThreadSanitizer | Medium |
| Version update logic | Property tests (existing) | Low (already tested) |

### 6. Areas Where Formal Methods Would Be Overkill

| Area | Reason |
|---|---|
| CIDR bitmask matching | Trivial bit operations, easily unit-tested |
| GDI/DXGI screen capture | OS API calls, no algorithm complexity |
| Registry read/write | OS API passthrough |
| File I/O | OS API passthrough |
| CLI argument parsing | Sequential, well-understood |
| NSIS installer packaging | Build-time only |

### 7. Minimum Useful Adoption Path

1. Extend TLA+ model to cover the auth/encryption handshake
2. Add protocol-level fuzz testing for the JSON parser
3. Run ThreadSanitizer in CI for data race detection
4. Convert property tests from hand-written random to full RapidCheck generators

---

## Stage 2 — Recommended Options

### Option A: Extend Existing TLA+ Model (Recommended)

**Approach:** Add the auth/encryption handshake protocol to the existing
`WinInspect_v2.tla` model. The handshake currently has no formal coverage
despite being the most security-critical component.

**What it applies to:** TCP handshake (hello → nonce → ECDH → signature →
AES-GCM session), currently ~90 lines in `tcp_server.cpp` lines 104-193.

**Properties to verify:**
- No unauthenticated client can establish an encrypted session
- Handshake timeout leads to connection close (no partial state)
- Nonce is fresh per connection (replay prevention)
- Shared secret is never leaked in plaintext
- Protocol version mismatch is rejected
- Server pubkey is unique per connection (PFS property)

**Expected benefit:** Catches handshake logic bugs before they become
security vulnerabilities. The model already found 5 bugs in the simpler
session/snapshot subsystem — the handshake has more state and worse coverage.

**Cost:** 2-3 days to extend the model + 1 day to validate.

**Tools:** Already installed (tla2tools.jar, TLC).

**Skill burden:** Low — team already understands TLA+.

**CI impact:** None — TLC runs complete in 2-5 minutes for small configs.

**Risks:** State explosion from modeling crypto operations symbolically
(mitigated by modeling abstractly — "key is established or not," not the
actual math).

**Success looks like:** 3 new invariants verified, 0 violations, covering
the complete handshake lifecycle.

**Bad fit if:** The team plans to replace the custom crypto with TLS (Phase 9),
in which case effort is better spent waiting for the replacement.

### Option B: Protocol Fuzzing + Property Tests

**Approach:** Add libFuzzer or similar coverage-guided fuzzing for the JSON
parser and protocol message handler. Expand RapidCheck property tests with
full generators.

**What it applies to:** `tinyjson.hpp` parser, `core.cpp` request dispatch,
`tcp_server.cpp` message framing.

**Properties to verify:** Parsing is crash-free for all inputs, dispatch
handles all method names without RCE, message length bounds are enforced.

**Expected benefit:** Finds memory safety bugs and parser edge cases.

**Cost:** 2-3 days setup + 1 day generator work.

**Tools:** libFuzzer (MSVC), AFL, or custom fuzz harness.

**Skill burden:** Medium — fuzzing infrastructure needs CI integration.

**CI impact:** High — fuzz tests run indefinitely. Needs time-bounded runs
and crash corpus management.

**Risks:** Fuzzing may not find meaningful bugs in a well-tested C++ codebase.
The JSON parser is simple (single-header, no recursion).

**Success looks like:** 0 crashes after 10M+ fuzz iterations.

### Option C: ThreadSanitizer + Concurrency Verification

**Approach:** Enable ThreadSanitizer (MSVC `/fsanitize=thread` or GCC
`-fsanitize=thread`) in CI for data race detection. TLA+ already covers
concurrency semantics; TSan catches implementation races.

**What it applies to:** All multi-threaded code in `tcp_server.cpp`,
`server.cpp`, `rendezvous_http.cpp`.

**Expected benefit:** Detects mutex misuses, lock ordering violations,
and unprotected shared state accesses.

**Cost:** 1-2 days integration + ongoing maintenance.

**Tools:** MSVC TSan (limited), GCC TSan (best via MinGW cross-compile).

**CI impact:** High — TSan significantly slows execution. Needs separate
CI job with longer timeout.

**Risks:** MSVC ThreadSanitizer support is incomplete on Windows. MinGW
cross-compilation for TSan may not be feasible in the current CI setup.

### Option D: Full Auth Protocol Proof (Deferred)

**Approach:** After TLS 1.3 migration (Phase 9), formally model the TLS
handshake properties using the existing TLA+ approach or a TLS-specific
verification tool (Tamarin, ProVerif).

**What it applies to:** TLS 1.3 handshake configuration after Phase 9.

**Properties to verify:** Standard TLS properties (secrecy, authentication,
forward secrecy).

**Expected benefit:** Assurance that the TLS configuration is correct.

**Cost:** 5-10 days after TLS migration.

**Skill burden:** Medium-high (TLS protocol expertise).

**CI impact:** None.

**Risks:** Premature if Phase 9 is deferred. The custom crypto will be
replaced before formal analysis adds value.

---

## Primary Recommendation: Option A — Extend Existing TLA+ Model

**Why this is the best choice:**

1. **Highest value per effort.** The auth handshake is the most security-critical
   component with zero formal coverage. The TLA+ infrastructure already exists.
   Previous TLA+ work found 5 bugs — expect similar or better returns here.

2. **Lowest risk.** The team already knows TLA+. No new tools to install.
   No CI changes. Model extensions are incremental (add state variables for
   handshake state, add actions for each message, add invariants for security
   properties).

3. **Complements the roadmap.** Phase 9 (TLS 1.3 migration) may replace the
   custom crypto eventually, but until then it needs verification. The TLA+
   model of the handshake also serves as a specification for the TLS replacement.

4. **Synchronization is cheap.** The handshake is ~90 lines of well-contained
   code in `tcp_server.cpp`. The model can be updated in minutes when the
   code changes.

## Fallback Recommendation: Option B — Protocol Fuzzing

If TLA+ extension is not acceptable (e.g., team does not want to maintain the
model), add protocol-level fuzzing instead. This is strictly weaker than model
checking (fuzzing finds implementation bugs, not design errors) but provides
defense-in-depth for the parser boundary.

---

## Stage 3 — Phased Bring-Up Ladder

### Phase 0 — Baseline

**Goal:** Confirm current formal methods artifacts are reproducible.

**Deliverables:**
- [ ] Run TLC on existing model: `cd formal/tla && java -jar tla2tools.jar WinInspect_v2.tla -workers 4 -depth 4`
- [ ] Capture output: states explored, invariants checked, runtime
- [ ] Run existing property tests: `ctest -R test_properties`
- [ ] Document current tool versions (TLC, RapidCheck, doctest)

**Commands:**
```bash
cd formal/tla
java -jar ../../tools/tla2tools.jar WinInspect_v2.tla -workers 4 -depth 4
```

**Pass criteria:** 0 invariant violations, all property tests pass.

### Phase 1 — Handshake Model Design

**Goal:** Design the TLA+ model extension for the auth/encryption handshake.

**Scope:** Add 4-5 new state variables and ~6 new actions to the existing
`WinInspect_v2.tla` model.

**New state variables:**
```
handshakeState   — [Clients -> {"idle", "hello_sent", "auth_pending", "established", "failed"}]
sessionKey       — [Clients -> KeyId \cup {NULL}]
serverNonce      — [Clients -> Nonce \cup {NULL}]
clientIdentity   — [Clients -> Identity \cup {NULL}]
```

**Deliverables:**
- [ ] Auth handshake state machine diagram
- [ ] New CONSTANTS: `Keys`, `Nonces`, `Identities`
- [ ] New VARIABLES: `handshakeState`, `sessionKey`, `serverNonce`
- [ ] New actions: `SendHello`, `ProcessAuth`, `EstablishSession`, `HandshakeTimeout`
- [ ] New invariants
- [ ] MC configuration files for handshake-only and full-model runs

### Phase 2 — First Useful Invariants

**Goal:** Verify the highest-value security properties.

**Invariants to check:**
```
AuthRequiredForEncryption ==
  \A c \in Clients :
    sessionKey[c] # NULL => handshakeState[c] = "established"

NoLeakedSecrets ==
  \A c1, c2 \in Clients :
    c1 # c2 => sessionKey[c1] # sessionKey[c2]

NonceUniqueness ==
  \A c1, c2 \in Clients :
    serverNonce[c1] = serverNonce[c2] => c1 = c2

HandshakeProgress ==
  \A c \in Clients :
    handshakeState[c] \in {"established", "failed"}
    \/ (handshakeState[c] = "hello_sent" /\ serverNonce[c] # NULL)
```

### Phase 3 — Repeatable Execution

**Goal:** Add Makefile targets and CI commands for the formal checks.

**Commands:**
```makefile
formal: formal-fast formal-full

formal-fast:
	cd formal/tla && java -jar ../../tools/tla2tools.jar \
	  WinInspect_v2.tla -workers 4 -depth 4 -config handshake_fast.cfg

formal-full:
	cd formal/tla && java -jar ../../tools/tla2tools.jar \
	  WinInspect_v2.tla -workers 4 -depth 6

formal-clean:
	rm -f formal/tla/*.states formal/tla/*.dot formal/tla/*.tla.*
```

**CI:** Add a `formal-check` job that runs `make formal-fast` (< 5 min).

### Phase 4 — Code-to-Model Traceability

**Goal:** Map every model variable and action to its code implementation.

| Model Variable | Code Location |
|---|---|
| `handshakeState[c]` | `tcp_server.cpp:104-193` — function scope |
| `sessionKey[c]` | `crypto::CryptoSession` object |
| `serverNonce[c]` | `nonce` vector in `handle_socket_client` |
| `clientIdentity[c]` | `AuthContext.identity` field |

| Model Action | Code Function |
|---|---|
| `SendHello` | `tcp_server.cpp:121-142` |
| `ProcessAuth` | `tcp_server.cpp:144-193` |
| `EstablishSession` | `tcp_server.cpp:195-201` |
| `HandshakeTimeout` | `setsockopt(SO_RCVTIMEO, 5000)` at line 112 |

### Phase 5 — Expand to Rendezvous Protocol

**Goal:** Model the rendezvous registration/heartbeat/deregister protocol.

**Scope:** New TLA+ model or model extension covering the rendezvous client
state machine and the rendezvous server state.

**Properties:**
- No phantom instances (heartbeat timeout → eviction)
- Instance registration is idempotent
- At most one deregister per registration
- TTL bounds are respected
- HMAC auth prevents unauthorized registrations

### Phase 6 — Maintenance and Governance

**Policy:**
- Formal artifacts updated in the same PR as handshake code changes
- PR checklist item: "Are formal model changes required?"
- CI runs formal-fast on every PR to master
- Nightly formal-full run on master
- Quarterly model review to check for drift

---

## Stage 4 — Repo Discipline

### Branch
```
feature/formal-auth-handshake
```

### Issues
- #xx — Assessment and recommendation
- #xx — Phase 1: Handshake model design
- #xx — Phase 2: First invariants verified
- #xx — Phase 3: CI integration
- #xx — Phase 4: Traceability map
- #xx — Phase 5: Rendezvous protocol model
- #xx — Phase 6: Governance policy

### PR Sequence
1. `feature/formal-auth-handshake` — Phases 0-2 (model + invariants)
2. `feature/formal-ci` — Phase 3 (CI integration)
3. `feature/formal-traceability` — Phase 4 (docs)
4. `feature/formal-rendezvous` — Phase 5 (rendezvous model)
5. `feature/formal-governance` — Phase 6 (policy docs)

### Commit Strategy
- One commit per invariant added
- One commit per CI config change
- Clear messages: `formal: add AuthRequiredForEncryption invariant`

---

## Stage 5 — Repeatable Run System

### Local Commands
```bash
make formal        # fast checks (< 5 min)
make formal-full   # deep checks (10-30 min)
make formal-clean  # clean temp files
```

### CI Commands (`.github/workflows/formal.yml`)
```yaml
- name: Run formal checks
  run: |
    cd formal/tla
    java -jar ../../tools/tla2tools.jar WinInspect_v2.tla \
      -workers 4 -depth 4 -config ci_fast.cfg
```

### Expected Outputs
```
TLC2 Version 2026.05.26
Running breadth-first search...
Model checking completed. No error found.
  Progress: 100% states, 0 invariant violations
  Stats: 124,567 states, 1,234,567 distinct, 2,345,678 transitions
```

---

## Stage 6 — Failure Analysis Workflow

When a counterexample is found:

1. Save the trace: `java -jar tla2tools.jar ... -dumpTrace trace.json`
2. Classify the failure:
   - Real design bug → fix code + add regression invariant
   - Model bug → fix spec + re-run
   - Assumption mismatch → document + re-assess
3. Create an issue linking to the trace
4. Fix the smallest safe thing
5. Re-run all formal checks + normal tests
6. Add the trace as a regression scenario

---

## Stage 7 — Expected Deliverables

1. **Assessment Report** — this document
2. **Extended TLA+ model** — `WinInspect_v3.tla` with auth handshake
3. **CI workflow** — `.github/workflows/formal.yml`
4. **Makefile targets** — `formal`, `formal-fast`, `formal-full`
5. **Traceability map** — added to `docs/MODEL_SYNC.md`
6. **Governance policy** — added to `CONTRIBUTING.md`
7. **Issues and PRs** — per the repo discipline plan above

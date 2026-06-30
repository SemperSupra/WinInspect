# Bring-Up Strategy: v0.3.0 — Multi-Instance LAN & Remote Access

## Overview

This document defines the phased implementation plan for transforming
WinInspect from a single-instance, localhost-only daemon into a
multi-instance, LAN-discoverable, remotely accessible system across
physical Windows boxes, VMs, and Wine containers.

The strategy builds on the research in `docs/MULTI_INSTANCE_LAN.md`
and applies the same layered architecture approach.

## Guiding Principles

1. **Each phase is independently releasable** — no phase depends on a later one.
   You can ship Phase 2 without Phase 3.
2. **Backward compatibility always** — existing single-instance, localhost,
   named-pipe workflows never break.
3. **Default behavior unchanged** — all new features are opt-in via flags.
4. **Formal methods track code** — the TLA+ model, property tests, and
   contract tests update alongside each phase.
5. **CI must stay green** — no failing tests at any commit.

---

## Branch Strategy

```
master
├── feature/v0.3.0-instance-identity      # Phase 1
├── feature/v0.3.0-port-ephemeral          # Phase 2
├── feature/v0.3.0-discovery-multicast     # Phase 3a
├── feature/v0.3.0-discovery-rendezvous    # Phase 3b
├── feature/v0.3.0-http-server             # Phase 4
├── feature/v0.3.0-web-ui                  # Phase 5
└── feature/v0.3.0-burst-capture           # Phase 6
```

Each branch:
- Branches from `master`
- Merges back via PR with squash commit
- Title format: `v0.3.0: <feature name>`
- Body includes checklist from this document

After all phases are merged, tag `v0.3.0` on `master`.

---

## Issue Hierarchy

Each phase gets one GitHub issue under milestone `v0.3.0`. The issue body
contains the full checklist for that phase.

```
Milestone: v0.3.0 — Multi-Instance LAN & Remote Access
├── #43  Phase 1:  Networking foundation (identity, dual-stack, config, rendezvous)
├── #54  Phase 2:  Network security (IP allow/deny, per-IP rate limiting)
├── #45  Phase 3a: Multicast discovery (mDNS/DNS-SD)
├── #46  Phase 3b: Rendezvous discovery (HTTP registry + heartbeat)
├── #61  Phase 3c: Rendezvous admin (kick, ban, access tiers, metadata)
├── #47  Phase 4:  HTTP server + REST API
├── #48  Phase 5:  WebUI dashboard
├── #49  Phase 6:  Burst capture (screen.record)
├── #50  Phase 7:  Integration smoke tests
├── #51  Phase 8:  Mutual authentication (daemon → client mTLS)
├── #55  Phase 9:  TLS 1.3 transport
├── #56  Phase 10: WebSocket event stream
├── #57  Phase 11: Unix domain sockets (Linux/Wine)
├── #58  Phase 12: Tailscale mesh VPN integration
├── #59  Phase 13: SOCKS5 proxy support
└── #60  Phase 14: Windows Registry config backend (deferred)
```

---

## Phase 1: Instance Identity

### Goal
Give every daemon instance a stable, user-readable identity that clients
can use to distinguish daemons on the network.

### Changes

**Daemon flags** (`daemon/src/server.cpp`):
```
--instance-name <string>    # human-readable name, e.g. "proxmox-vm-01"
--include-hostname          # existing flag, already used in discovery
```

On startup, the daemon generates a persistent UUID stored in:
- Windows: `%LOCALAPPDATA%\WinInspect\instance.id`
- Linux/Wine: `~/.config/wininspect/instance.id`

**Core struct** (`core/include/wininspect/types.hpp`):
```cpp
struct InstanceIdentity {
  std::string name;        // user-supplied --instance-name or hostname
  std::string uuid;        // persistent, auto-generated
  std::string hostname;    // OS hostname (opt-in via --include-hostname)
};
```

**Backend method** (`IBackend`):
```cpp
virtual InstanceIdentity get_instance_identity() = 0;
```

**Protocol method** `daemon.identity` returns:
```json
{
  "name": "proxmox-vm-01",
  "uuid": "a1b2c3d4-...",
  "hostname": "pve01.local"
}
```

**Discovery response** augmented to include instance name + UUID.

### Files Modified
- `core/include/wininspect/types.hpp` — add `InstanceIdentity` struct
- `core/include/wininspect/backend.hpp` — add virtual method
- `core/include/wininspect/win32_backend.hpp` — add override
- `core/include/wininspect/fake_backend.hpp` — add override
- `core/src/win32_backend.cpp` — uuid generation + file persistence
- `core/src/fake_backend.cpp` — stub returning test identity
- `core/src/core.cpp` — `daemon.identity` dispatch entry
- `daemon/src/server.cpp` — `--instance-name` flag parsing
- `daemon/src/tcp_server.cpp` — include identity in handshake
- `clients/cli/src/cli.cpp` — `identity` CLI command

### Tests

| Test Type | What | How |
|---|---|---|
| **Contract** | `daemon.identity` returns valid JSON | FakeBackend stub + CoreEngine |
| **Contract** | UUID format matches `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` | Regex test |
| **Unit** | UUID is stable across daemon restarts | Write UUID, restart, verify same |
| **Unit** | `--instance-name` overrides hostname | Parse flag, compare |
| **TLA+ model** | `identity` is pure QueryMethod (no state change) | Add to QueryMethods set |

### Smoke Check
```bash
wininspectd --headless --instance-name "test-box"
# In another terminal:
wininspect identity
# → { "name": "test-box", "uuid": "a1b2...", "hostname": "..." }
```

### PR Checklist
- [ ] `daemon.identity` contract test passes
- [ ] UUID persist/reload unit test passes
- [ ] `--instance-name` flag accepted
- [ ] FakeBackend stub returns valid identity
- [ ] TLA+ model updated (QueryMethods set)
- [ ] All existing tests still pass

---

## Phase 2: Dynamic Port Acquisition

### Goal
Daemon binds to an OS-assigned ephemeral port when no `--port` is given or
when `--port 0` is specified. The assigned port is written to a file for
reference by discovery and container port mapping.

### Changes

**Daemon flags:**
```
--port 0          # request OS ephemeral port (new behavior for value 0)
--port-file <path> # write assigned port to file on startup
```

Existing behavior (`--port 1985`) unchanged.

**TCP server** (`daemon/src/tcp_server.cpp`):
```cpp
// If requested_port == 0, bind with htons(0)
// After bind(), getsockname() to retrieve actual port
// If --port-file specified, write port to file
```

**Discovery broadcast** includes the actual bound port (not the requested port).

### Files Modified
- `daemon/src/tcp_server.cpp` — bind port 0 support, getsockname
- `daemon/src/server.cpp` — `--port-file` flag, wire port to discovery
- `clients/cli/src/cli.cpp` — `discover` shows actual port

### Tests

| Test Type | What | How |
|---|---|---|
| **Unit** | `bind(port=0)` returns port in ephemeral range | Integration test on Windows |
| **Unit** | `--port-file` written on startup, readable | File I/O test |
| **Contract** | Discovery response includes actual port | FakeBackend returns fixed port |
| **Property** | Multiple rapid starts get unique ports | Bind 10 sockets in loop, assert all different |
| **TLA+ model** | No state change — pure socket concern | No model change needed |

### Smoke Check
```bash
wininspectd --headless --port 0 --port-file /tmp/wininspect.port
cat /tmp/wininspect.port  # should show port like 52134
wininspect --tcp localhost:52134 identity
# → responds correctly on the ephemeral port
```

### PR Checklist
- [ ] `--port 0` binds ephemeral port
- [ ] `--port-file` written at startup
- [ ] Discovery advertises actual port
- [ ] Property test: 10 concurrent binds all unique
- [ ] All existing tests still pass

---

## Phase 3a: Multicast Discovery (mDNS/DNS-SD)

### Goal
Replace the current UDP broadcast discovery with standard mDNS/DNS-SD.
Daemon advertises `_wininspect._tcp` service with instance name, UUID,
port, and capability TXT records. Clients discover via standard mDNS query.

### Changes

**Service advertisement** (`daemon/src/server.cpp`):
```
_wininspect._tcp  port=<ephemeral or configured>
  TXT: instance=<name>
  TXT: uuid=<uuid>
  TXT: os=<os_string>
  TXT: dxgi=<true|false>
  TXT: proto_ver=<version>
```

**Implementation options (in priority order):**
1. **Avahi D-Bus** (Linux/Wine) — `avahi-publish-service` via `dbus` or
   subprocess call. Widely available, standard.
2. **Windows native DNS-SD** — Windows 10+ has built-in mDNS via
   `Windows.Networking.ServiceDiscovery.Dnssd` (WinRT). For Win32:
   use the `DnsServiceRegister` API (Windows 10 1803+).
3. **Bonjour `dnssd.dll`** — Apple's mDNSResponder, cross-platform fallback.
4. **Embedded mDNS** — `mdns` single-header C library for minimal dependency.

**Recommended:** Option 2 for Windows, Option 1 for Linux/Wine. Fall back
to current UDP broadcast if neither is available.

**Client discovery** (`clients/cli/src/cli.cpp`):
```cpp
// 1. Query via DnsServiceBrowse (Windows) or avahi-browse (Linux)
// 2. Parse responses: instance name, UUID, port, TXT records
// 3. Deduplicate by UUID
// 4. Fall back to UDP broadcast if mDNS unavailable
```

### Files Modified
- `daemon/src/server.cpp` — mDNS advertisement thread
- `daemon/CMakeLists.txt` — link `dnssd` on Windows (optional)
- `clients/cli/src/cli.cpp` — mDNS client discovery
- `core/include/wininspect/types.hpp` — `ServiceInfo` TXT record types

### Tests

| Test Type | What | How |
|---|---|---|
| **Unit** | TXT record construction | Format check against RFC 6763 |
| **Integration** | Windows mDNS registration + query | Requires two Windows processes |
| **Integration** | Linux mDNS via Avahi mock | `avahi-publish-service` + `avahi-browse` |
| **Contract** | Discovery response format | Backend returns structured discovery info |
| **TLA+ model** | Discovery is outside state machine | No model change |

### Smoke Check
```bash
# On daemon host:
wininspectd --public --instance-name "test-box"
# On client host (same LAN):
wininspect discover
# → test-box  (192.168.1.10:52134, Windows 11, dxgi ✓)
```

### PR Checklist
- [ ] mDNS service registration on startup
- [ ] mDNS service deregistration on shutdown
- [ ] TXT records include instance name, UUID, capabilities
- [ ] Client discovery query works
- [ ] Fallback to UDP broadcast when mDNS unavailable
- [ ] Integration test on Linux (Avahi) and Windows
- [ ] All existing tests still pass

---

## Phase 3b: Rendezvous Discovery (HTTP Registry)

### Goal
Add a rendezvous-based discovery path for environments where multicast
does not work (Docker bridge, cross-VLAN). This is the fallback for the
hybrid discovery approach.

### Changes

**HTTP rendezvous client** in daemon:
```cpp
// On startup:
POST http://<rendezvous>/api/v1/instances
  { "name": "...", "uuid": "...", "host": "...", "port": ...,
    "capabilities": {...}, "ttl": 30 }

// Every 30s (heartbeat):
PUT http://<rendezvous>/api/v1/instances/<uuid>

// On shutdown:
DELETE http://<rendezvous>/api/v1/instances/<uuid>
```

**Rendezvous server** — a new, minimal HTTP service (separate repo or
included in the daemon as optional mode):

```
rendezvous --port 8080 --data-dir /var/lib/wininspect-registry

GET  /api/v1/instances        → list all instances
GET  /api/v1/instances/<uuid> → single instance
POST /api/v1/instances        → register (body: instance info)
PUT  /api/v1/instances/<uuid> → heartbeat
DELETE /api/v1/instances/<uuid> → deregister
```

**File-based rendezvous** (zero-server alternative):
```
\\fileserver\share\wininspect\
├── <uuid>.json    # written by daemon on startup
├── <uuid>.json    # removed by daemon on shutdown
└── ...            # heartbeat = file modification time
```

**Daemon flags:**
```
--rendezvous http://rendezvous:8080   # HTTP rendezvous endpoint
--rendezvous \\fileserver\share       # SMB rendezvous path
```

### Files Modified
- `daemon/src/server.cpp` — rendezvous client thread
- `clients/cli/src/cli.cpp` — query rendezvous in discovery
- `daemon/src/rendezvous_client.cpp` — new file
- `daemon/src/rendezvous_server.cpp` — new file (optional mode)

### Tests

| Test Type | What | How |
|---|---|---|
| **Unit** | Rendezvous client serialization | Mock server, verify POST body |
| **Integration** | Daemon registers + heartbeat | Run rendezvous server, verify |
| **Integration** | Stale entry cleanup after TTL | Wait for expiry, assert deletion |
| **Contract** | Discovery merges mDNS + rendezvous | Mock both, verify merge |
| **TLA+ model** | Registration is outside state machine | No model change |

### Smoke Check
```bash
# Start rendezvous server:
wininspectd --rendezvous-mode --rendezvous-port 8080

# On another host (container):
wininspectd --public --instance-name "container-1" \
  --rendezvous http://rendezvous-host:8080

# Client:
wininspect discover --rendezvous http://rendezvous-host:8080
# → container-1  (172.17.0.5:52134, Wine 9.0, dxgi ✗)
```

### PR Checklist
- [ ] Daemon registers on startup via HTTP/SMB
- [ ] 30s heartbeat keeps entry alive
- [ ] Cleanup on graceful shutdown
- [ ] Client queries rendezvous and merges with multicast results
- [ ] Integration test: daemon + rendezvous server
- [ ] All existing tests still pass

---

## Phase 4: HTTP Server + REST API

### Goal
Embed a lightweight HTTP server in the daemon that exposes the existing
protocol methods as a REST API, enabling browser-based and script-based
remote access.

### Changes

**HTTP server** — single-header embedded server (minimal dependency):
- `third_party/http/http.hpp` — single-header HTTP server (or embedded
  WinHTTP server API)
- Supports: GET, POST, JSON request/response, WebSocket upgrade

**REST endpoints:**
```
GET    /api/v1/health           → daemon.health
GET    /api/v1/identity         → daemon.identity
GET    /api/v1/capabilities     → daemon.capabilities
POST   /api/v1/capture          → screen.capture {left,top,right,bottom}
GET    /api/v1/windows          → window.listTop
GET    /api/v1/windows/<hwnd>   → window.getInfo
POST   /api/v1/click            → input.mouseClick {x,y,button}
POST   /api/v1/keys             → input.text {text}
POST   /api/v1/hotkey           → input.hotkey {keys}
...
```

**WebSocket endpoint:**
```
WS /api/v1/events  → pushes: {event, data, timestamp}
```

**Auth:** Bearer token via `--http-token <secret>`. Token sent in
`Authorization: Bearer <token>` header. Empty token = localhost-only.

**Daemon flags:**
```
--http-port <port>    # enable HTTP server (default: 0 = disabled)
--http-token <secret> # bearer token for auth
```

### Files Created
- `third_party/http/http.hpp` — single-header HTTP server
- `daemon/src/http_server.hpp` — HTTP server class declaration
- `daemon/src/http_server.cpp` — REST API implementation
- `daemon/src/websocket.hpp` — WebSocket handler

### Files Modified
- `daemon/src/server.cpp` — start HTTP server thread
- `CMakeLists.txt` — add http_server.cpp

### Tests

| Test Type | What | How |
|---|---|---|
| **Unit** | Each endpoint returns correct JSON | Mock backend, test HTTP handler |
| **Contract** | `/api/v1/capture` returns valid ScreenCapture | FakeBackend + HTTP request |
| **Integration** | HTTP server + CoreEngine end-to-end | Start daemon, curl each endpoint |
| **Security** | Auth token required for remote access | Curl without token → 401 |
| **Property** | All protocol methods have corresponding REST endpoint | Auto-generate endpoint list |
| **TLA+ model** | HTTP = new transport, no state change | No model change |

### Smoke Check
```bash
wininspectd --headless --http-port 8080 --http-token "secret123"
# In another terminal:
curl -H "Authorization: Bearer secret123" http://localhost:8080/api/v1/identity
# → { "name": "...", "uuid": "..." }
curl -H "Authorization: Bearer secret123" \
  -X POST -d '{"left":0,"top":0,"right":100,"bottom":100}' \
  http://localhost:8080/api/v1/capture
# → { "width": 100, "height": 100, "data_b64": "..." }
```

### PR Checklist
- [ ] HTTP server starts on `--http-port`
- [ ] All protocol methods have REST endpoints
- [ ] Bearer token auth enforced
- [ ] WebSocket events endpoint works
- [ ] Contract tests for each endpoint
- [ ] Integration test: curl each endpoint
- [ ] All existing tests still pass

---

## Phase 5: WebUI Dashboard

### Goal
A browser-based dashboard that provides real-time visibility into all
daemon instances and enables basic desktop operations.

### Changes

**Static web assets** — served by the daemon's HTTP server:
- `daemon/webui/index.html` — main dashboard
- `daemon/webui/app.js` — React or vanilla JS dashboard
- `daemon/webui/style.css` — styling

**Dashboard features:**
1. **Daemon list** — all discovered instances (name, UUID, OS, capabilities)
2. **Window tree** — browse windows on a selected daemon
3. **Screen preview** — live screenshot (polled or WebSocket)
4. **Action panel** — click, type, hotkey, drag
5. **Connection status** — which daemon currently connected

**Implementation approach:** Vanilla JS, no build step. Single HTML file
with embedded CSS and JS. Uses Fetch API for REST calls and WebSocket for
event streaming. Served directly by the daemon's HTTP server.

**WebSocket event stream:**
```
message: {"event": "window_changed", "hwnd": "0x1234", "title": "Notepad"}
message: {"event": "capture_frame", "width": 1920, "height": 1080,
          "data_b64": "..."}
message: {"event": "daemon_state", "connections": 2}
```

### Files Created
- `daemon/webui/index.html` — dashboard HTML (embedded JS + CSS)
- `daemon/webui/` directory added to daemon build

### Files Modified
- `daemon/src/http_server.cpp` — serve static files from `webui/`
- `CMakeLists.txt` — install webui directory

### Tests

| Test Type | What | How |
|---|---|---|
| **Unit** | Static file serving works | HTTP GET / → returns HTML |
| **Integration** | WebSocket event push | Connect WS, trigger event, verify message |
| **Contract** | Dashboard doesn't break existing API | All REST tests still pass |
| **Smoke** | Human opens `http://localhost:8080` in browser | Manual check |

### Smoke Check
```bash
wininspectd --headless --http-port 8080
# Open http://localhost:8080 in browser
# → Dashboard loads, shows daemon identity, window list
```

### PR Checklist
- [ ] Dashboard loads in Chrome, Firefox, Edge
- [ ] WebSocket events render in dashboard
- [ ] Capture preview displays correctly
- [ ] Action buttons call correct REST endpoints
- [ ] All existing tests still pass

---

## Phase 6: Burst Capture (screen.record)

### Goal
Add the ability to capture a short sequence of frames (burst recording),
not just a single screenshot. This enables animation capture, UI transition
tracking, and short video clips.

### Changes

**Protocol method** `screen.record`:
```json
// Request:
{ "method": "screen.record",
  "params": { "left": 0, "top": 0, "right": 1920, "bottom": 1080,
              "frames": 30, "interval_ms": 100 }}

// Response:
{ "ok": true,
  "result": { "width": 1920, "height": 1080,
              "frames": [
                {"index": 0, "data_b64": "..."},
                {"index": 1, "data_b64": "..."},
                ...
              ],
              "fps": 10.0 }}
```

**Backend:**
```cpp
virtual ScreenRecording record_screen(Rect region, int frames,
                                       int interval_ms) = 0;
```

`ScreenRecording` struct:
```cpp
struct ScreenRecording {
  int width{}, height{};
  std::vector<ScreenCapture> frames;
  double actual_fps{};
};
```

**Implementation:** Loop calling `capture_screen()` at the specified
interval. The DXGI and GDI backends already handle this — they're
synchronous. For DXGI, throw away intermediate frames if the capture
rate exceeds the monitor refresh rate (just return what we get).

**File export:** Optional `--format` parameter to encode as an animated
GIF or video file on the server side. Requires stb_image_write or similar
for GIF. For v0.3.0, just returns raw frames — encoding is a future phase.

### Files Modified
- `core/include/wininspect/types.hpp` — add `ScreenRecording` struct
- `core/include/wininspect/backend.hpp` — add virtual method
- `core/include/wininspect/win32_backend.hpp` — add override
- `core/include/wininspect/fake_backend.hpp` — add override
- `core/src/win32_backend.cpp` — implement record loop
- `core/src/fake_backend.cpp` — stub returning 3 frames
- `core/src/core.cpp` — `screen.record` dispatch entry
- `clients/cli/src/cli.cpp` — `record` CLI command

### Tests

| Test Type | What | How |
|---|---|---|
| **Contract** | `screen.record` returns correct JSON | FakeBackend returns 3 frames |
| **Unit** | Frame count matches request | Record 10 frames, verify count |
| **Unit** | Interval timing is approximate | Measure duration, assert ±20% |
| **Property** | Each frame is valid ScreenCapture | Random intervals, verify schema |
| **Integration** | DXGI burst capture | Physical Windows, record 30 FPS for 3s |
| **TLA+ model** | `record` is a QueryMethod | Add to QueryMethods set |

### Smoke Check
```bash
wininspectd --headless
wininspect record 0 0 100 100 --frames 10 --interval 50
# → 10 frames, ~500ms duration, valid BMP data in each
```

### PR Checklist
- [ ] `screen.record` contract test passes
- [ ] Frame count matches request (within tolerance)
- [ ] CLI command works
- [ ] FakeBackend returns valid frames
- [ ] TLA+ model updated (QueryMethods set)
- [ ] All existing tests still pass

---

## Phase 7: Integration Smoke Tests

### Goal
A suite of cross-instance smoke tests that validate the full multi-instance
LAN system works end-to-end across simulated environments.

### Test Scenarios

Each scenario runs as a GitHub Actions job or local script.

**Scenario 1: Two local daemons (same machine)**
```bash
# Daemon A on default port
wininspectd --headless --instance-name "alpha" --port 19851
# Daemon B on different port
wininspectd --headless --instance-name "beta" --port 19852
# Client discovers both
wininspect discover
# → alpha (127.0.0.1:19851, ...)
# → beta  (127.0.0.1:19852, ...)
```

**Scenario 2: Ephemeral port acquisition**
```bash
wininspectd --headless --port 0 --port-file /tmp/port.txt
PORT=$(cat /tmp/port.txt)
wininspect --tcp 127.0.0.1:$PORT identity
# → responds successfully
```

**Scenario 3: mDNS discovery (Linux with Avahi)**
```bash
# Requires Avahi running on host
wininspectd --public --instance-name "mdns-test"
wininspect discover
# → mdns-test (127.0.1.1:..., ...)
```

**Scenario 4: HTTP rendezvous**
```bash
# Start rendezvous server
wininspectd --rendezvous-mode --rendezvous-port 8080 &
# Start daemon with rendezvous
wininspectd --public --instance-name "rendezvous-test" \
  --rendezvous http://127.0.0.1:8080
# Query rendezvous
curl http://127.0.0.1:8080/api/v1/instances
# → [{ name: "rendezvous-test", ... }]
```

**Scenario 5: REST API end-to-end**
```bash
wininspectd --headless --http-port 8080 --http-token "test"
# Full API walkthrough:
curl -H "Authorization: Bearer test" http://localhost:8080/api/v1/health
curl -H "Authorization: Bearer test" http://localhost:8080/api/v1/identity
curl -X POST -H "Authorization: Bearer test" \
  -d '{"left":0,"top":0,"right":100,"bottom":100}' \
  http://localhost:8080/api/v1/capture
# All return 200 with valid JSON
```

**Scenario 6: Wine container (Docker)**
```dockerfile
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y xvfb wine wine64
COPY wininspectd.exe /app/
CMD Xvfb :99 -screen 0 1024x768x24 & \
    DISPLAY=:99 wine wininspectd.exe --public --port 1985
```
```bash
docker build -t wininspect-test .
docker run --rm -p 19850:1985 wininspect-test
wininspect --tcp localhost:19850 identity
# → responds from within container
```

### Files Created
- `tests/smoke/test_multi_instance.sh` — bash script running all scenarios
- `.github/workflows/smoke.yml` — GitHub Actions matrix for smoke tests

### Environment Matrix

| Environment | CI Runner | Notes |
|---|---|---|
| Windows native | `windows-latest` | Tests 1, 2, 4, 5 |
| Linux (Wine) | `ubuntu-latest` | Tests 3 (Avahi), 6 (Docker) |
| Docker | `ubuntu-latest` | Test 6 |

### TLA+ Model Update for v0.3.0

After all phase code is merged, run the model update:

```bash
python scripts/update_tla_model.py
```

New model elements needed:
- `InstanceIdentity` struct → no state machine change (identity is derived)
- Ephemeral port → no state change (socket concern)
- Discovery (multicast + rendezvous) → no state change (external)
- HTTP server → no state change (new transport)
- `screen.record` → add to `QueryMethods` set (side-effect-free)
- Burst capture → no new variables (same capture path, just looped)

Run TLC:
```bash
cd formal/tla
java -Xmx2g -jar tla2tools.jar WinInspect_v2.tla -workers 4 -depth 4
```

Expected: 0 invariant violations, all 8 invariants pass.

### PR Checklist
- [ ] All 6 smoke scenarios pass on appropriate runners
- [ ] TLA+ model passes with 0 violations
- [ ] Property-based tests pass (200 iterations each)
- [ ] All contract tests pass
- [ ] All pre-existing tests pass
- [ ] CI matrix is green

---

## Dependency Graph

```
Phase 1 (Identity)
  └── needed by Phase 3a (mDNS needs instance name + UUID)
      └── needed by Phase 3b (rendezvous needs UUID)
          └── needed by Phase 4 (REST API endpoints reference identity)
              └── needed by Phase 5 (WebUI shows instances)

Phase 2 (Ephemeral port)
  └── needed by Phase 3a (mDNS advertises actual port)
  └── needed by Phase 3b (rendezvous registers actual port)

Phase 3a + 3b (Discovery) → independent of each other, both need 1 + 2

Phase 4 (HTTP server) → needs 1 for identity, but can work without discovery

Phase 5 (WebUI) → needs 4 (HTTP server)

Phase 6 (Burst capture) → independent of all other phases

Phase 7 (Smoke tests) → needs all phases complete
```

**Recommended merge order:**
1. Phase 1 (Identity) — foundational
2. Phase 2 (Ephemeral port) — foundational
3. Phase 6 (Burst capture) — independent, low risk
4. Phase 3a (mDNS) — needs 1 + 2
5. Phase 3b (Rendezvous) — needs 1 + 2
6. Phase 4 (HTTP server) — needs 1
7. Phase 5 (WebUI) — needs 4
8. Phase 7 (Smoke tests) — needs all

---

## Quick-Reference Table

```
┌─────────┬──────────────────────────────────────┬──────────┬──────────┬───────────┐
│ Phase   │ Feature                              │ Branches │ Issues   │ Effort    │
├─────────┼──────────────────────────────────────┼──────────┼──────────┼───────────┤
│ 1       │ Networking foundation (identity,      │ 1        │ 1        │ 2-3 days  │
│         │   dual-stack, config, rendezvous)     │          │          │           │
│ 2       │ Network security (IP allow/deny,      │ 1        │ 1        │ 2-3 days  │
│         │   per-IP rate limiting)               │          │          │           │
│ 3a      │ Multicast discovery (mDNS)            │ 1        │ 1        │ 3-5 days  │
│ 3b      │ Rendezvous discovery (HTTP registry)  │ 1        │ 1        │ 3-5 days  │
│ 4       │ HTTP server + REST API                │ 1        │ 1        │ 5-10 days │
│ 5       │ WebUI dashboard                       │ 1        │ 1        │ 3-5 days  │
│ 6       │ Burst capture (screen.record)         │ 1        │ 1        │ 2-3 days  │
│ 7       │ Integration smoke tests               │ 1        │ 1        │ 2-3 days  │
│ 8       │ Mutual authentication (mTLS)          │ 1        │ 1        │ 3-5 days  │
│ 9       │ TLS 1.3 transport                     │ 1        │ 1        │ 5-10 days │
│ 10      │ WebSocket event stream                │ 1        │ 1        │ 2-3 days  │
│ 11      │ Unix domain sockets (Linux/Wine)      │ 1        │ 1        │ 2-3 days  │
│ 12      │ Tailscale mesh VPN integration        │ 1        │ 1        │ 3-5 days  │
│ 13      │ SOCKS5 proxy support                  │ 1        │ 1        │ 2-3 days  │
│ 14      │ Registry config backend (optional)    │ 1        │ 1        │ 2-3 days  │
├─────────┼──────────────────────────────────────┼──────────┼──────────┼───────────┤
│         │ Total                                │ 16       │ 16       │ 45-70 days│
└─────────┴──────────────────────────────────────┴──────────┴──────────┴───────────┘
```

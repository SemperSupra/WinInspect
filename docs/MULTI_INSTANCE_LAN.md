# Multi-Instance LAN Deployment

## Overview

WinInspect daemons may be deployed across a mixed topology of physical
Windows boxes, virtual machines (Proxmox/Hyper-V/QEMU/KVM), and Wine
containers (Docker/WineBot) on the same LAN. This document analyzes the
networking, capture, and identity implications of each environment.

---

## Current Architecture (v0.1.2)

| Feature | Current Behavior |
|---|---|
| Default bind | `127.0.0.1:1985` (localhost only) |
| LAN access | `--public` binds `0.0.0.0:1985` |
| Discovery | UDP broadcast on port 1986, responds with port + OS + pipe name + hostname (opt-in) |
| Instance identity | Hostname only (via `--include-hostname`), no instance UUID or user-defined label |
| Auth | SSH key signatures (optional, `--auth-keys`) |
| Multi-instance | `--pipe-name` for local pipe isolation, no TCP port flag (use `--port`) |

---

## Environment Comparison

### Physical Windows Box

| Factor | Behavior |
|---|---|
| **DXGI capture** | ✅ Fully supported. D3D11CreateDevice succeeds, DuplicateOutput works against the console session |
| **GDI fallback** | Always available as fallback |
| **DXGI exclusivity** | Only one `AcquireNextFrame` handle per D3D device per output. OBS, Remote Desktop, or another DXGI client can cause `DXGI_ERROR_ACCESS_DENIED` → automatic GDI fallback |
| **Default discovery** | UDP broadcast responds on the LAN. Hostname is opt-in (`--include-hostname`) |
| **Port conflict** | No — each physical machine has a unique IP, bound to `0.0.0.0:1985` |

### Virtual Machine (Proxmox / Hyper-V / QEMU / KVM)

| Factor | Behavior |
|---|---|
| **DXGI capture** | ❌ Generally unavailable |
|  | Proxmox: No GPU passthrough by default. D3D11CreateDevice fails → `dxgi_available_` = false |
|  | Hyper-V: Basic Display Adapter has no WDDM driver → no DXGI |
|  | QEMU/KVM: virtio-gpu provides basic 2D, no DXGI. PCI passthrough of a physical GPU works but is rare |
| **GDI fallback** | ✅ Works if a user session is active (console or RDP) |
|  | Headless VM with no logged-in user: `GetDC(NULL)` returns NULL → `capture_screen()` returns `nullopt` |
| **LAN discovery** | ✅ VM on a virtual bridge shares the host's broadcast domain — UDP discovery works |
| **Capture availability** | Requires an active interactive session (RDP, console, or AutoLogon) |

### Wine Container (Docker / WineBot)

| Factor | Behavior |
|---|---|
| **DXGI capture** | ❌ Not supported. Wine does not implement DXGI Desktop Duplication |
|  | The `D3D11CreateDevice` probe fails → `dxgi_available_` = false, GDI fallback |
| **GDI behavior** | ✅ Works within Wine's virtual desktop if configured |
|  | `wine explorer /desktop=name,1024x768` creates a virtual framebuffer |
|  | Without virtual desktop, target is the X11 root window (requires X server) |
| **LAN discovery** | ❌ Docker bridge mode isolates UDP broadcasts |
|  | UDP port 1986 must be published (`-p 1986:1986/udp`) but routing UDP discovery through NAT is unreliable |
|  | Solutions: `--net=host`, explicit `-p` for both TCP and UDP, or skip discovery entirely via `--tcp` |
| **Port mapping** | TCP port 1985 must be published (`-p 19850:1985`) or host networking used |
|  | Each container on the same host needs a unique host-side port |

---

## Key Tradeoffs

### 1. Instance Identity (Discovery Ambiguity)

The UDP discovery response currently includes:
- OS string ("windows 11", "windows (wine)")
- Port number
- Pipe name
- Hostname (**only** with `--include-hostname`, which is off by default for privacy)

It does **not** include:
- An instance UUID or stable identifier
- A user-defined label or name
- Container name or VM name
- The `dxgi_capture` capability flag

**Implication:** A client scanning the LAN sees N daemon responses but cannot
distinguish "the physical box" from "the Proxmox VM" without `--include-hostname`
and knowing hostnames in advance. Explicit `--tcp host:port` targeting is the
only reliable disambiguation method today.

### 2. Bridge Network Isolation

Docker bridge mode (default for containers) prevents UDP discovery from
crossing the bridge into the host LAN. Even if a container publishes
`-p 1986:1986/udp`, the broadcast sender on the host LAN won't reach the
container's UDP listener because Docker's bridge does not forward broadcasts.

**Workarounds:**
- Use `--net=host` (loses container port isolation)
- Publish both TCP and UDP, and have clients connect via explicit `--tcp host:port`
- Run a small discovery proxy on the host that relays container discovery

### 3. Authentication Boundary

With `--public` and no `--auth-keys`, any process on the LAN can call
any protocol method, including mutations (`process.execute`, `reg.write`,
`clipboard.write`). The `--read-only` flag gates mutations but still allows
enumeration of processes, windows, and files.

In a mixed topology where containers share a host with other workloads:
- **Without auth:** A compromised container can control the host's daemon
- **With `--auth-keys`:** Each client must present a valid SSH signature.
  Key distribution is manual but provides strong isolation.

### 4. DXGI Exclusivity

Only one DXGI output duplication handle can be active per D3D device per
output. If another process (OBS, Remote Desktop, screen recorder) has an
active duplication handle, WinInspect's `AcquireNextFrame` will fail with
`DXGI_ERROR_ACCESS_DENIED`. The code handles this by falling back to GDI
automatically — no crash, but the performance advantage of DXGI is lost.

### 5. Session Dependence

Capture (whether DXGI or GDI) requires an active Windows session with a
desktop. In order of reliability:

| Environment | Session Required | Notes |
|---|---|---|
| Physical, user logged in | Console | Most reliable |
| Physical, Remote Desktop | RDP session | GetDC works via RDP |
| VM with AutoLogon | Console | Setup at VM provisioning |
| VM, headless (no login) | None | GetDC fails → no capture |
| Wine container | X server or virtual desktop | Must be configured explicitly |
| Session 0 (Windows service) | None | GetDC fails → no capture |

---

## Recommended Approach

### Near-term (config discipline, no code changes)

```bash
# Physical box
wininspectd.exe --public --port 1985 --include-hostname --auth-keys allowed_keys

# Proxmox/Hyper-V VM
wininspectd.exe --public --port 1985 --include-hostname --auth-keys allowed_keys

# Wine container (bridge mode)
wininspectd.exe --public --port 1985 --auth-keys allowed_keys
# Docker: -p 19850:1985 (no UDP port needed — clients use --tcp directly)
```

Clients connect via `--tcp hostname:1985` (LAN) or `--tcp host:19850` (mapped
container). Discovery is supplementary, not required.

### Medium-term (add --instance-name flag)

A single new flag — `--instance-name "proxmox-vm-01"` — would:
1. Set a human-readable instance name
2. Include it in the UDP discovery response so `wininspect discover` shows:
   ```
   proxmox-vm-01       192.168.1.50:1985   Windows 11   dxgi ✗
   physical-desktop    192.168.1.10:1985   Windows 11   dxgi ✓
   winebot-container-3 172.17.0.5:1985     Wine 9.0     dxgi ✗
   ```
3. Expose it in `daemon.health` for client-side identification

This follows the existing `--include-hostname` pattern without the privacy
concern (user-defined name vs hostname).

### Longer-term (if topology grows)

- **Discovery filters:** Query by capability (`--os windows --has-dxgi`),
  instance name pattern, or tag
- **Mutual auth:** Require the daemon to prove its identity to the client
  (not just client → daemon)
- **TLS transport:** If daemons cross network boundaries without physical
  security (e.g., across VLANs or the internet)

---

## Config Templates

### Physical Box

```ini
[wininspectd]
port = 1985
public = true
include-hostname = true
auth-keys = C:\wininspect\allowed_keys
```

### Proxmox VM (AutoLogon user)

```ini
[wininspectd]
port = 1985
public = true
include-hostname = true
auth-keys = /etc/wininspect/allowed_keys
```

### Docker Container

```dockerfile
EXPOSE 1985
# UDP 1986 is optional — clients use --tcp
CMD ["wininspectd", "--public", "--port", "1985", "--auth-keys", "/etc/wininspect/allowed_keys"]
```

```yaml
# docker-compose.yml
services:
  wininspect:
    image: wininspectd:latest
    network_mode: host   # easiest for discovery
    # OR bridge mode with explicit port:
    # ports:
    #   - "19850:1985"
    environment:
      - WININSPECT_AUTH_KEYS=/etc/wininspect/allowed_keys
      - DISPLAY=:0        # if using X11
      - WINEDLLOVERRIDES=dxgi=d # disable DXGI in Wine
```

### Client Connection (all environments)

```bash
# Explicit targeting (recommended)
wininspect --tcp physical-box:1985 capture 0 0 1920 1080
wininspect --tcp proxmox-vm:1985 list-top
wininspect --tcp docker-host:19850 process.list

# Discovery (same broadcast domain only — excludes containers)
wininspect discover
```

---

## Research: Solutions for Identified Network Issues

### 1. Dynamic Port Acquisition

#### Problem
Multiple daemons on the same host (e.g., multiple Wine containers behind one
host IP) need unique TCP ports. Hardcoded ports cause conflicts.

#### Method: bind(port=0) — Standard OS Ephemeral Allocation

The OS assigns an available port from the ephemeral range when `bind()` is
called with port 0. After binding, `getsockname()` returns the assigned port.

```cpp
// Windows (Winsock)
SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
sockaddr_in addr = {};
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;
addr.sin_port = htons(0);  // OS assigns
bind(sock, (sockaddr*)&addr, sizeof(addr));
// Read back:
int len = sizeof(addr);
getsockname(sock, (sockaddr*)&addr, &len);
int assigned_port = ntohs(addr.sin_port);
```

This works identically on Windows (Winsock), Linux (POSIX sockets), and
Wine (implements Winsock over Linux sockets). **No port iteration needed.**

**Tradeoffs:**
- ✅ Works on all target environments (Windows, Wine, Linux native)
- ✅ No TOCTOU race — the port is reserved the instant bind() succeeds
- ❌ Port is not known until after bind — cannot pre-announce in a config file
- ❌ Ephemeral range varies: Windows defaults to 49152–65535 (Vista+),
  Linux defaults to 32768–60999

#### Method: Range Probe with SO_REUSEADDR

If a specific port range is required (e.g., 19850–19890 for predictable
port mapping), probe sequentially:

```cpp
for (int port = 19850; port <= 19890; port++) {
    addr.sin_port = htons(port);
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
        // acquired
        break;
    }
}
```

**Tradeoffs:**
- ✅ Predictable — port mapping in Docker/VM can target a range
- ❌ TOCTOU race window between probe and use (mitigated by SO_REUSEADDR)
- ❌ Slower than OS ephemeral allocation

#### Method: Ephemeral + Rendezvous Registration

Best approach for multi-container: bind ephemeral, then register the actual
port with a rendezvous point (file, registry, or discovery service).

**Recommendation:** Use `bind(port=0)` + `getsockname()` as the default.
The port is reported via the discovery protocol (see Section 2) and cached
in `--port-file` for reference:

```
--port-file C:\run\wininspect.port   # write assigned port to file
```

---

### 2. Cross-Environment Service Discovery

#### Problem
UDP broadcast (current method) does not cross Docker bridge networks. Need
a discovery method that works across physical LAN, VM bridges, and containers.

#### Method A: mDNS / DNS-SD (Multicast DNS)

| Platform | Support | Notes |
|---|---|---|
| Windows 10/11 | ✅ Built-in (since 1803) | `System.Net.NetInformation` or Bonjour |
| Linux | ✅ Avahi / systemd-resolved | `avahi-publish-service` / D-Bus API |
| Docker (bridge) | ❌ Blocked (no multicast across bridge) | Same limitation as UDP broadcast |
| Docker (host) | ✅ | Multicast works with `--net=host` |
| Wine | ✅ Via host mDNS | Uses Linux host's Avahi/mDNS stack |

mDNS provides service type + instance name + port in a standard format:
```
_wininspect._tcp.local.  port=1985  hostname=proxmox-vm-01.local.
```

**DNS-SD Service Registration (Linux/Avahi):**
```bash
avahi-publish-service "wininspect-proxmox-01" _wininspect._tcp 1985 \
  "os=windows-11" "dxgi=true" "instance=proxmox-vm-01"
```

**Tradeoffs:**
- ✅ Standard protocol, wide platform support
- ✅ No central registry needed — fully peer-to-peer
- ❌ Same multicast limitation as UDP broadcast — doesn't cross Docker bridge
- ❌ Requires Avahi (Linux) or Bonjour (Windows, if not 10+ built-in)

#### Method B: Rendezvous / Registry-Based Discovery

A central registry (file share, HTTP endpoint, or database) that daemons
write to and clients read from.

**File-based (SMB share):**
```
\\fileserver\wininspect-registry\
├── physical-box.json    # {"host":"192.168.1.10","port":1985,"os":"windows 11","dxgi":true}
├── proxmox-vm.json
└── docker-host.json     # {"host":"192.168.1.200","port":19850,...}
```

Each daemon writes its manifest on startup, removes on shutdown. Clients
read the directory to discover all instances.

**HTTP rendezvous (lightweight):**
```bash
# Daemon registers on startup:
curl -X PUT http://rendezvous:8080/instances/physical-box \
  -d '{"host":"192.168.1.10","port":1985,"capabilities":["dxgi"]}'

# Client discovers:
curl http://rendezvous:8080/instances
# Returns list of all registered daemons
```

**Tradeoffs:**
- ✅ Works across ALL environments (no multicast dependency)
- ✅ No network topology constraints — SMB, HTTP, or any accessible store works
- ✅ Instance identity built-in (file/folder name is the instance identifier)
- ❌ Requires a shared filesystem or rendezvous server (single point of failure)
- ❌ Stale entries on unclean shutdown (mitigated by TTL/heartbeat)

#### Method C: Hybrid (Multicast + Rendezvous Fallback)

The recommended approach — try mDNS/multicast first, fall back to rendezvous:

1. Daemon starts, binds ephemeral port
2. **Advertise via mDNS/UDP** (fast path for same-LAN)
3. **Also write to rendezvous** (file share or HTTP, for cross-bridge)
4. Daemon sends periodic heartbeats (every 30s) via both paths
5. Clients query both: collect mDNS responses + read rendezvous
6. Deduplicate by instance UUID

```
Client Discovery Flow:
1. Send mDNS query for _wininspect._tcp (250ms timeout)
2. Also query rendezvous HTTP endpoint (parallel)
3. Merge results, deduplicate by UUID
4. Present unified list to user/agent
```

**Tradeoffs:**
- ✅ Best coverage — same-LAN uses fast multicast, cross-bridge uses rendezvous
- ✅ Graceful degradation — if one path fails, the other still works
- ✅ Instance identity via UUID — unambiguous even if hostnames collide
- ❌ More complex implementation (two discovery paths, merge logic)

#### Method D: Docker-specific — Embedded DNS + Sidecar

For Docker environments only, leverage Docker's built-in DNS on custom
networks:

```yaml
# docker-compose.yml
services:
  wininspect:
    image: wininspectd:latest
    network: wininspect-net
    hostname: wininspect-01      # DNS name within the network

  discovery-sidecar:
    image: consul:latest          # or etcd, or a simple HTTP registry
    ports:
      - "8500:8500"
```

Other containers discover via `wininspect-01:1985` within the Docker network.
A sidecar container bridges discovery to the host LAN.

**Tradeoffs:**
- ✅ Best for Docker-native workflows
- ❌ Only works within Docker — doesn't help physical-to-container discovery
- ❌ Adds complexity (sidecar container, Consul/etcd overhead)

#### Recommendation

Use **Hybrid (Method C)**: multicast + rendezvous.
- mDNS for same-LAN peers (fast, zero-config)
- Rendezvous via a simple HTTP endpoint or SMB file share for cross-bridge
- Instance UUID for unambiguous identity
- 30s heartbeat TTL for stale-entry cleanup

---

### 3. Screenshot / Capture Methods by Environment

#### Physical Windows (Console Session)

| Method | Available | Performance | Notes |
|---|---|---|---|
| GDI BitBlt | ✅ Always | Medium | Current implementation, works everywhere |
| DXGI Desktop Duplication | ✅ Win8+ | High | Current implementation, GPU-accelerated |
| **Windows.Graphics.Capture** (WGC) | ✅ Win10 1803+ | High | Modern API, supports window-level capture |
| Direct3D backbuffer read | ✅ | High | Game capture, requires D3D device from target |
| Mirror driver (DFX) | ❌ Deprecated | — | Removed in Win8, not supported |

**Windows.Graphics.Capture (WGC)** is the newer Microsoft-recommended API.
Unlike DXGI Desktop Duplication, WGC:
- Does NOT require a D3D11 device — uses `GraphicsCaptureSession`
- CAN capture individual windows (not just the full desktop)
- Works in more environments (Remote Desktop sessions, some virtual GPUs)
- Is exposed through WinRT (`Windows.Graphics.Capture`), needs C++/WinRT or COM interop

**To use WGC from a Win32 (WinInspect) process:**
```cpp
// Requires C++/WinRT headers
#include <winrt/Windows.Graphics.Capture.h>
#include <windows.graphics.capture.interop.h>

auto interop = winrt::get_activation_factory<
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
    IGraphicsCaptureItemInterop>();
winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = {nullptr};
interop->CreateForWindow(hwnd, winrt::guid_of<decltype(item)>(), winrt::put_abi(item));
```

**Tradeoff vs DXGI:**
- WGC: Window-level capture, more portable, no GPU requirement, but ~10-15% overhead vs DXGI
- DXGI: Full desktop only, higher performance, exclusivity constraint

#### Windows VM (Proxmox / Hyper-V / QEMU)

| Method | Works? | Conditions |
|---|---|---|
| GDI BitBlt | ✅ | Requires active user session (console or RDP) |
| DXGI | ❌ Usually | No WDDM GPU driver in standard VMs |
| WGC | ❌ Usually | Same limitation as DXGI — needs GPU driver |
| RDP-based capture | ✅ | If connected via RDP, BitBlt works against RDP session |
| Hyper-V Enhanced Session | ✅ | Passes through D3D, but DXGI duplication still fails |

**Best approach for VMs:** GDI BitBlt with AutoLogon configuration.
Set up the VM to auto-login a user account, then `capture_screen()` works
via GDI fallback immediately. No DXGI needed.

#### Wine / Docker (Headless)

| Method | Works? | Conditions |
|---|---|---|
| GDI BitBlt | ✅ | Requires Wine virtual desktop or X server |
| DXGI | ❌ | Wine doesn't implement IDXGIOutputDuplication |
| WGC | ❌ | Requires WinRT, not available in Wine |
| **Xvfb + GDI** | ✅ | Xvfb provides virtual display, Wine renders to it |
| **Xvfb + x11vnc** | ✅ | VNC access to the virtual framebuffer |
| **Wine virtual desktop** | ✅ | `wine explorer /desktop=name,1024x768` |

**Docker setup for Wine capture:**
```dockerfile
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y \
    xvfb x11vnc wine wine64
ENV DISPLAY=:99
CMD Xvfb :99 -screen 0 1920x1080x24 & \
    wine wininspectd.exe --public --port 1985
```

Xvfb creates a virtual framebuffer at 1920×1080×24bpp. Wine renders to it.
GDI BitBlt (`GetDC(NULL)`) captures from this framebuffer. Everything works
end-to-end with no physical display.

#### Session 0 (Windows Service — No Interactive Desktop)

| Method | Works? | Notes |
|---|---|---|
| GDI BitBlt | ❌ | No desktop Session 0 — `GetDC(NULL)` returns NULL |
| DXGI | ❌ | Same reason |
| WGC | ❌ | Same reason |
| **Session 0 → Session 1 IPC** | ✅ | Daemon runs in Session 1, not Session 0 |
| **RDP-to-self** | ⚠️ | Complex, requires interactive session |

**Recommendation:** The daemon should NOT run as a Session 0 service if
screen capture is needed. Run it as a user-level process in the interactive
session (Session 1+). The daemon already supports this via `--headless`
(no tray icon) and named pipe for local clients.

---

### 4. Desktop Recording / Streaming Methods

#### Single Screenshot (Current)

`capture_screen()` returns one frame as a base64-encoded BMP. This is the
current implementation. It's sufficient for point-in-time inspection but
not for video recording or real-time streaming.

#### Frame Sequence (Burst Capture)

For short recordings (e.g., 5 seconds at 10 FPS), call `capture_screen()` in
a loop. This works with both GDI and DXGI backends:

```cpp
std::vector<ScreenCapture> burst;
for (int i = 0; i < 50; i++) {  // 5 seconds at 10 FPS
    burst.push_back(capture_screen(rect));
    Sleep(100);
}
```

**Limitations:** High memory usage (each frame is a full BMP), network
overhead for transfer.

#### DXGI-based Recording (DirectX Video Encoding)

DXGI can feed frames directly into a video encoder (hardware H.264/H.265):

```
DXGI OutputDuplication → AcquireNextFrame → ID3D11Texture2D
  → IMFTransform (hardware encoder) → H.264 bitstream
  → file or stream
```

Requires Media Foundation or a library like FFmpeg. Not currently in
WinInspect — would be a new `screen.record` feature.

**FFmpeg as subprocess approach (lower effort):**
```cpp
// Launch ffmpeg to capture via gdigrab/dxgi
// This works without linking FFmpeg — just shell out
std::string cmd = "ffmpeg -f gdigrab -framerate 15 -i desktop "
                  "-c:v libx264 -preset ultrafast -f mp4 output.mp4";
system(cmd.c_str());
```

**Tradeoffs:**
- ✅ `ffmpeg -f gdigrab` works everywhere GDI works (VMs, Wine, physical)
- ✅ `ffmpeg -f dxgi` for GPU-accelerated capture (physical only)
- ✅ Can output to a file, pipe, or network stream
- ❌ Requires FFmpeg installed as a dependency
- ❌ FFmpeg as subprocess has limited integration (no progress reporting)

#### VNC-based Streaming

Run a VNC server alongside the daemon for real-time remote viewing:

| VNC Server | Platform | Notes |
|---|---|---|
| TightVNC | Windows | Free, works with GDI |
| x11vnc | Linux/Wine | Captures X11 framebuffer — works with Xvfb |
| UltraVNC | Windows | Supports video driver for faster capture |

```bash
# x11vnc + Xvfb for Wine container
Xvfb :99 -screen 0 1920x1080x24 &
x11vnc -display :99 -forever -nopw &
wine wininspectd.exe ...
```

**Tradeoffs:**
- ✅ Real-time remote desktop access, cross-platform
- ✅ Works with Xvfb — headless containers are viewable
- ❌ Security — VNC typically uses weak auth or none
- ❌ Separate protocol, separate tool — not integrated into WinInspect

#### WebRTC-based Streaming

For low-latency streaming to a web browser:

```
capture_screen() → frames → WebRTC peer connection → browser
```

Requires a WebRTC stack (e.g., `libwebrtc`, `pion/webrtc` for Go, or
`aiortc` for Python). High complexity, not recommended for v0.x.

#### Recommendation

For v0.2.x, **short recordings via burst capture** (calling `capture_screen()`
in a loop at ~10 FPS for 5-10 seconds) is the most practical addition.
FFmpeg as an optional subprocess for longer recordings.

Longer-term: VNC in Wine containers (x11vnc + Xvfb) for real-time visual
access, independent of WinInspect's protocol.

---

### 5. Remote Access Methods (CLI / GUI / WebUI / API / MCP)

#### Current State

| Interface | Transport | Platform | Notes |
|---|---|---|---|
| CLI (`wininspect.exe`) | Named pipe (local) or TCP | Windows | Current, works |
| GUI (`wininspect-gui.exe`) | Named pipe only | Windows | Current, local only |
| Daemon (`wininspectd.exe`) | Named pipe + TCP | Windows | Current |

#### CLI over TCP (Already Works)

```bash
wininspect --tcp 192.168.1.50:1985 capture 0 0 1920 1080
```

This is the primary remote interface today. Auth via `--auth-keys` for
SSH signature verification.

#### WebUI (New — Recommended for v0.2.0+)

A lightweight HTTP server in the daemon that serves:
1. **REST API** — JSON endpoints wrapping the existing protocol methods
2. **Static web UI** — HTML/JS dashboard for human operators

**Architecture:**
```
wininspectd (port 1985, TCP protocol)
  └── built-in HTTP server (port 1986, or separate --http-port)
       ├── GET /api/capture → JSON with screenshot data
       ├── GET /api/windows → list of windows
       ├── POST /api/click  → send mouse click
       ├── WS /api/events   → WebSocket for event streaming
       └── GET /            → static web dashboard HTML
```

**Implementation options:**
- Embed a minimal HTTP server (C++: `cpp-httplib`, `libmicrohttpd`, or use
  WinHTTP's server API)
- Keep it single-header, no external dependency (`tinyhttp` pattern, similar
  to existing `tinyjson.hpp`)

**Tradeoffs:**
- ✅ Accessible from any browser — no client installation
- ✅ REST API is language-agnostic — any tool can call it
- ✅ WebSocket for real-time events (window changes, capture frames)
- ❌ Adds HTTP server surface area (security: CORS, CSRF, auth tokens)
- ❌ Additional code to maintain alongside the TCP protocol

**Auth strategy:**
```text
Option A: Shared secret in URL query (?token=xxx) — simple, less secure
Option B: Bearer token in Authorization header — standard REST pattern
Option C: Session cookie after login — best for human operators
Option D: Bind to localhost only, tunnel through SSH/nginx — most secure
```

#### MCP Server (Model Context Protocol)

MCP allows AI agents (Claude, etc.) to interact with the daemon directly.

**Architecture:**
```
Claude Desktop / Claude Code
  └── MCP client (stdio or HTTP)
       └── WinInspect MCP server (new process)
            ├── connects to daemon via TCP
            └── exposes tools: list_windows, capture_screen, click, type, etc.
```

**MCP tool definitions (example):**
```json
{
  "name": "wininspect_capture_screen",
  "description": "Capture a region of the screen. Returns base64-encoded BMP.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "left": {"type": "integer"},
      "top": {"type": "integer"},
      "right": {"type": "integer"},
      "bottom": {"type": "integer"}
    },
    "required": ["left", "top", "right", "bottom"]
  }
}
```

**Implementation:** An MCP server is a standalone process (Python, Node.js,
or Go) that:
1. Connects to `wininspectd` via TCP
2. Implements the MCP protocol (stdio-based JSON-RPC)
3. Exposes each protocol method as an MCP tool

**Tradeoffs:**
- ✅ Gives AI agents direct desktop control (the WineBot use case)
- ✅ Well-defined protocol (MCP spec by Anthropic)
- ✅ Can be a separate project — doesn't bloat the daemon
- ❌ Separate process to manage (but it's lightweight)
- ❌ Requires MCP client (Claude Desktop, etc.) — only meaningful in AI context

#### WebSocket Event Streaming

For real-time monitoring, add a WebSocket endpoint that pushes events:

```
Client connects → WebSocket handshake → server pushes:
  {"event": "window_created", "hwnd": "0x1234", "title": "Notepad"}
  {"event": "capture_frame", "data_b64": "..."}
  {"event": "daemon_state", "connections": 3, "sessions": 1}
```

This is the lowest-latency way to get screen data to a remote consumer.

#### Tunneling and Secure Remote Access

For accessing daemons across NATs or the internet:

| Method | Works | Complexity | Notes |
|---|---|---|---|
| SSH tunnel | ✅ | Low | `ssh -L 1985:localhost:1985 host` — standard, secure |
| WireGuard VPN | ✅ | Medium | Full mesh VPN, best for multi-site |
| Tailscale / ZeroTier | ✅ | Low | Mesh VPN with single-click setup |
| ngrok / bore / frp | ✅ | Low | TCP tunnel with public endpoint |
| Cloudflare Tunnel | ✅ | Medium | HTTP-only, integrates with Cloudflare |

**Recommendation:** Use Tailscale for the internal LAN — it provides a
mesh VPN, works through NATs, assigns unique IPs to each machine/container,
and handles mDNS forwarding across nodes. This solves both the discovery
and secure access problems at once.

#### Integrated Recommendation

A **layered remote access stack** for WinInspect:

```
┌──────────────────────────────────────────────┐
│  Layer 5: MCP Server                          │
│  (Separate process, talks to daemon via TCP)  │
│  → AI agent integration (Claude, etc.)        │
├──────────────────────────────────────────────┤
│  Layer 4: WebUI + REST API                    │
│  (Built-in HTTP server on daemon)             │
│  → Browser access, curl scripts, WebSocket    │
├──────────────────────────────────────────────┤
│  Layer 3: CLI over TCP                        │
│  (Current `wininspect --tcp`)                 │
│  → Scripting, CI/CD, power users              │
├──────────────────────────────────────────────┤
│  Layer 2: Discovery                           │
│  (mDNS + rendezvous hybrid)                   │
│  → Find daemons across all environments       │
├──────────────────────────────────────────────┤
│  Layer 1: Mesh VPN (Tailscale / WireGuard)    │
│  → Secure transport, NAT traversal            │
└──────────────────────────────────────────────┘
```

Each layer is independent — you can use CLI over Tailscale without the
WebUI, or the WebUI over SSH without Tailscale.

---

## Implementation Priority

| Priority | Feature | Effort | Impact |
|---|---|---|---|
| P0 | `--port-file` + ephemeral port | 1 day | Solves container port conflicts |
| P0 | Hybrid discovery (mDNS + rendezvous) | 3-5 days | Cross-environment daemon finding |
| P1 | WebUI + REST API | 5-10 days | Browser-based remote access |
| P1 | VNC in Wine containers (config only) | 0.5 days | Real-time visual access to containers |
| P2 | MCP server (separate project) | 3-5 days | AI agent integration |
| P2 | Burst capture / short recording | 2-3 days | Frame sequences, not just single shots |
| P3 | WGC capture backend | 3-5 days | Window-level capture, RDP compatibility |
| P3 | FFmpeg subprocess recording | 1-2 days | Longer video recordings |

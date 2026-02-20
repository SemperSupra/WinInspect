# Protocol v1 (local IPC)

## Transport
- **Windows Named Pipe**: `\\.\pipe\wininspectd` (Local IPC, no auth required by default)
- **TCP**: `0.0.0.0:1985` (Cross-environment/Network, SSH Auth required if enabled)
- **Framing**: 4-byte little-endian length prefix + UTF-8 JSON payload.
- **Protocol Version**: 1.0.0

## Authentication & Encryption (Handshake)
1. **Server $\rightarrow$ Client**: `{"type":"hello","version":"1.0.0","nonce":"<b64>","pubkey":"<b64_ecdh_pub>"}`
2. **Client $\rightarrow$ Server**: `{"version":"1.0.0","identity":"<user>","signature":"<b64_ssh_sig>","pubkey":"<b64_ecdh_pub>"}`
3. **Server $\rightarrow$ Client**: `{"type":"auth_status","ok":true}`

**Post-Handshake:**
All subsequent messages are encrypted using **AES-256-GCM**.
Framing: `[4-byte Length][12-byte Nonce][16-byte Tag][Ciphertext]`
The shared secret is derived via ECDH (X25519 or P-256).

## Canonical JSON
For golden tests and idempotence checks, clients may request `canonical:true`.
In v1, canonicalization is implemented as a stable key-sorted JSON serializer
(sufficient for project outputs). If you require full RFC 8785 JCS, replace
`core/src/canonical_json.cpp` with a complete JCS implementation.

## Envelope
Request:
```json
{"id":"c1-1","method":"window.getInfo","params":{"snapshot_id":"s-1","hwnd":"0x1","canonical":true}}
```

Response:
```json
{"id":"c1-1","ok":true,"result":{...}}
```

Error:
```json
{"id":"c1-1","ok":false,"error":{"code":"E_BAD_HWND","message":"not a valid window handle"}}
```

## Methods
- `snapshot.capture`: Captures a new global snapshot. Returns `snapshot_id`.
- `window.listTop`: List top-level windows in a snapshot.
- `window.listChildren`: List immediate children of a window.
- `window.getInfo`: Get detailed metadata for a window handle.
- `window.pickAtPoint`: Find window at screen coordinates `x,y`.
- `window.ensureVisible`: Force a window to be shown/hidden.
- `window.ensureForeground`: Bring a window to the front.
- `window.postMessage`: Post a Win32 message to a window.
- `input.send`: Send raw `INPUT` structures (base64).
- `input.mouseClick`: High-level mouse click.
- `input.keyPress`: High-level key press (VK code).
- `input.text`: Send UTF-8 text as keyboard input.

### UI Automation (UIA)
- `ui.inspect`: Perform recursive UIA discovery on a window.
  - Params: `hwnd`
  - Returns: Tree of elements with `automation_id`, `name`, `control_type`, and `children`.
- `ui.invoke`: Trigger the `Invoke` pattern on a specific element.
  - Params: `hwnd`, `automation_id`
  - Returns: `{"invoked": true}`

### Events
- `events.subscribe`: Enable event tracking for this session.
- `events.unsubscribe`: Disable event tracking.
- `events.poll`: Retrieve pending events.
  - Behavior: If `old_snapshot_id` is not provided, the daemon compares the current state against the state at the time of the *last* poll for this session.
  - Returns: Array of `{"type": "window.created|destroyed", "hwnd": "0x..."}`.

## Event Subscription Model
WinInspect uses a **State-Sync Polling** model for events. 
1. Client calls `events.subscribe`.
2. Client calls `events.poll` periodically.
3. The daemon maintains a "last known state" for each subscribed client.
4. On `poll`, the daemon captures a new snapshot, diffs it against the last known state, returns the diff, and updates the last known state.
This ensures no events are missed even if the client polls slowly, and it avoids the complexity of server-side push in heterogeneous Wine environments.

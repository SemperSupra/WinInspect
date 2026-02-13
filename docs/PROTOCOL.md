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
- `snapshot.capture`
- `window.listTop`
- `window.listChildren`
- `window.getInfo`
- `window.pickAtPoint`
- `window.ensureVisible` (desired-state)
- `window.ensureForeground` (desired-state)
- `window.postMessage` (injection)
- `input.send` (injection: raw base64 data)
- `events.subscribe`
- `events.unsubscribe`
- `events.poll`

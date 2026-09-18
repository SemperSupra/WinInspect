# Privacy Policy

*Last updated: 2026-07-03*

WinInspect is a local-first desktop automation tool. It processes data on
your machine and does not collect, transmit, or sell personal data to any
third party.

---

## Data Processing (Local Only)

WinInspect accesses the following data on your machine in order to function:

| Data | Feature | Purpose | Configurable |
|------|---------|---------|-------------|
| Window titles, class names, positions | `window.*` RPC methods | Identify and manipulate windows | `--allow`/`--deny` method filtering |
| Screen pixels | `screen.capture`, `screen.getPixel` | Visual desktop inspection | `--read-only` restricts capture |
| Clipboard content | `clipboard.read`, `clipboard.write` | Copy/paste automation | `--no-clipboard` disables entirely |
| Registry keys/values | `reg.*` RPC methods | System configuration | `--read-only` prevents writes |
| Process list | `process.list` | Identify running applications | `--allow`/`--deny` filtering |
| Environment variables | `env.get` | Read system configuration | Secrets redacted by default |
| File system | `file.*` RPC methods | File operations | `--allow`/`--deny` filtering |
| Keyboard/mouse input | `input.*` RPC methods | Automation | `--read-only` disables |

All of this data stays on your machine. It is not transmitted to any third
party unless you configure a remote connection (see Network section below).

---

## Network Communications

WinInspect listens for connections on your local machine. Depending on your
configuration, it may also transmit data over the network:

### Local IPC (Default, No Network)

The daemon listens on a Windows Named Pipe (`\\.\pipe\wininspectd`). This
is a local-only communication channel — no network data is sent or received.

### TCP (Optional, Requires `--bind`)

When configured to listen on a TCP port (default: 1985), the daemon accepts
remote connections. Data transmitted includes:
- Window titles and metadata (in response to RPC requests)
- Screen capture data (in response to `screen.capture`)
- All data listed in the Data Processing section above

**Protections:**
- `--auth-keys` enables ECDH key exchange + AES-256-GCM encryption
- `--http-token` authenticates HTTP API requests
- `--bind` restricts which network interface the daemon listens on
- Default bind is `127.0.0.1` (localhost only)

### HTTP Dashboard (Optional, Requires `--http-port`)

The built-in HTTP server serves a web dashboard and REST API. When enabled:
- The Bearer token (if configured via `--http-token`) is sent in plaintext
  with every request — this is a LAN-only feature
- Dashboard HTML and API responses contain window and desktop data

### UDP Discovery (Optional, Enabled by Default)

The daemon broadcasts a UDP discovery response on the LAN when queried.
The response includes:
- Operating system type and version
- TCP port number
- Pipe name
- Hostname (if `--include-hostname` is set)
- Tailscale IP (if detected)

This is a reply-only protocol — the daemon does not initiate broadcasts.

### Rendezvous Registration (Optional, Requires Configuration)

If configured with `--rendezvous`, the daemon registers with a rendezvous
server. The registration includes:
- A persistent UUID (generated at first run, stored in `instance.id`)
- A human-readable instance name (defaults to hostname)
- The OS hostname
- The TCP port number
- An ECDH public key (for authentication)

Heartbeats are sent every 30 seconds. The registration persists until the
daemon shuts down or is evicted by the rendezvous server.

### Update Checks (Optional, Enabled by Default)

Every 24 hours, the daemon may check for updates by making an HTTPS request
to `api.github.com/repos/SemperSupra/WinInspect/releases/latest`. This is a
standard GitHub API call — GitHub may see your IP address and the fact that
you are running WinInspect. No user-identifying information is sent.

Disable with `--enable-update-check=false` or in the config file.

---

## Data Storage

| File | Contents | Location | Retention |
|------|----------|----------|-----------|
| `config.json` | Network config, bind addresses, rendezvous URLs | `%APPDATA%\WinInspect\` | Until uninstall |
| `instance.id` | Persistent UUID | `%APPDATA%\WinInspect\` | Until uninstall |
| `.wininspect_config` | SSH key path for CLI auth | `~/.wininspect_config` | Until manual delete |

### Audit Log (In-Memory)

The daemon maintains an audit log of RPC method calls (up to 10,000 entries).
This log is stored in memory and is lost when the daemon shuts down.
It is not written to disk.

The audit log records: method name, parameters, controller identity,
timestamp, and duration. Parameter data may include window titles,
clipboard content, or other information from RPC requests.

Access the audit log: `wininspect control audit-log` (requires TCP connection).

### Logger Buffer (In-Memory)

The daemon keeps the last 100 log messages in memory. This buffer is lost
when the daemon shuts down. The `--log-level` flag controls verbosity.
With `--admin-logs`, the buffer can be queried via `daemon.logs`.

---

## Privacy Controls

| Control | Flag / Setting | Effect |
|---------|---------------|--------|
| Disable clipboard access | `--no-clipboard` | Blocks clipboard read/write entirely |
| Read-only mode | `--read-only` | Prevents all mutation operations |
| Restrict network interface | `--bind 127.0.0.1` | Listen on localhost only |
| Encrypt TCP traffic | `--auth-keys <file>` | Enables ECDH + AES-256-GCM |
| Authenticate HTTP API | `--http-token <token>` | Bearer token required for HTTP |
| Disable update checks | `--enable-update-check=false` | No outbound network requests |
| Disable discovery | `--discovery-port 0` | No UDP broadcast responses |
| Disable rendezvous | Omit `--rendezvous` | No registration with external servers |
| Method allow/deny | `--allow`, `--deny` | Filter which RPC methods are available |
| Limit method audit | Control manager | Audit log of method calls (in-memory) |

---

## Third-Party Services

| Service | Purpose | Data Sent | Configurable |
|---------|---------|-----------|-------------|
| GitHub API (`api.github.com`) | Update check | IP address, User-Agent | `--enable-update-check=false` |
| Rendezvous server (user-configured) | Instance discovery | UUID, hostname, port, pubkey | Omit `--rendezvous` |

No other third-party services are contacted.

---

## Data Deletion

To delete all data stored by WinInspect:

1. Stop the daemon
2. Delete `%APPDATA%\WinInspect\` (config, identity)
3. Delete `~/.wininspect_config` (SSH key path)
4. Uninstall via `winget uninstall WinInspect` or Add/Remove Programs

The NSIS uninstaller prompts: "Remove WinInspect configuration and instance
identity?" — selecting YES deletes all stored data.

---

## Changes to This Policy

Updates to this policy will be reflected in the `PRIVACY.md` file in the
project repository: https://github.com/SemperSupra/WinInspect

---

## Contact

For privacy inquiries: mark.e.deyoung+wininspect-privacy@gmail.com

Or open an issue: https://github.com/SemperSupra/WinInspect/issues

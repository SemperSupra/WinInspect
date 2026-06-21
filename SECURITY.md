# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | :white_check_mark: |

Only the most recent release receives security updates.

## Reporting a Vulnerability

**Do not open a public issue.** Instead, report vulnerabilities privately:

- **Email**: Create a [GitHub Security Advisory](https://github.com/SemperSupra/WinInspect/security/advisories/new) (preferred)
- Or contact the maintainer directly

You can expect an initial response within 72 hours and a timeline for resolution within one week.

## Security Considerations for Users

### Daemon Binding

The daemon (`wininspectd.exe`) binds to `127.0.0.1` by default. When run
with the `--public` flag, it binds to all interfaces (including the network):

```bash
# Safe: localhost only (default)
wininspectd.exe

# Exposed: use only on trusted networks or with auth
wininspectd.exe --public --auth-keys authorized_keys
```

When using `--public`, enable authentication with `--auth-keys` to require
Ed25519 SSH key verification for all connections.

### Authentication

The daemon supports Ed25519 challenge-response authentication over TCP.
To enable it:

```bash
# Create an authorized_keys file
echo "ssh-ed25519 AAAAC3... user@host" > authorized_keys

# Start daemon with auth
wininspectd.exe --auth-keys authorized_keys
```

### Privilege Model

- The daemon runs with the privileges of the user who started it
- Memory read/write operations require `PROCESS_VM_READ` / `PROCESS_VM_WRITE`
- Service control requires appropriate SCManager permissions
- Input injection uses `SendInput`, which is subject to UIPI (User Interface Privilege Isolation)

### Update Verification

All release artifacts are signed with SHA256 checksums. Verify downloads:

```powershell
CertUtil -hashfile WinInspect-Installer-v0.1.0.exe SHA256
```

Compare the output against the `.sha256` file published alongside each release.

### Read-Only Mode

For inspection-only use cases, start the daemon in read-only mode:

```bash
wininspectd.exe --read-only
```

This blocks all mutating operations (`window.postMessage`, `input.send`,
`reg.write`).

## Secure Development

- Builds are automated via GitHub Actions from public source code
- All dependencies are pinned to specific versions
- Submodule changes are governed by the Co-Evolution Policy (see `docs/POLICIES.md`)
- Formal verification: the concurrent client model is specified in TLA+
  (`formal/tla/WinInspect_v1.tla`)

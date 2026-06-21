# Privacy Policy

WinInspect does not collect, transmit, or store any personal data.
All processing is performed locally on the user's machine.

- **No telemetry** — the application does not phone home
- **No analytics** — no usage data is collected
- **No user accounts** — no authentication data is stored
- **No network communication** — except for the optional update check
  feature (`daemon.checkUpdate`), which makes a single HTTPS request to
  `api.github.com` to compare the current version against the latest
  GitHub Release. No user-identifying information is sent in this request.

The daemon listens on `127.0.0.1` by default. When run with `--public`,
it binds to all interfaces — users are responsible for securing their
network in that configuration.

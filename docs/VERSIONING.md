# Versioning Policy

## Release Versioning
Format: `vMAJOR.MINOR.PATCH` (e.g. `v0.1.1`)

- **MAJOR**: Breaking protocol changes, new architecture
- **MINOR**: New features, new protocol methods, significant changes
- **PATCH**: Bug fixes, documentation, build changes

## Protocol Versioning
Format: `MAJOR.MINOR` (e.g. `0.1`)

The protocol version is declared in the hello handshake and must match
between client and server.

- **MINOR bump**: New methods added (backward compatible)
  - Old clients can still connect — unknown methods return E_BAD_METHOD
  - New clients work with old servers — server ignores unknown methods
- **MAJOR bump**: Method removed, renamed, or behavior changed
  - Strict version match required — connection rejected on mismatch

## Schema Versioning
The `protocol/schema_v1.json` file documents the wire format.
Its `$id` field follows the protocol version.
The schema is normalized to valid JSON for machine validation.

## Artifact Version Pools

| Pool | Artifacts | Change Trigger |
|---|---|---|
| Release | WININSPECT_VERSION, git tag, CMake | New release |
| Protocol | PROTOCOL_VERSION, schema, docs | Wire format change |
| Build | cmake, go, container images | Toolchain upgrade |

Release and protocol can change independently.

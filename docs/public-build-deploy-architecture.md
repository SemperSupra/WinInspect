# Public build/deploy architecture

WinInspect is a public product repository. Heavy build, test, packaging, release, provenance, and product-owned lifecycle checks belong here or in the public Windows Package Foundry so they can run on public GitHub-hosted runners without consuming private-repository Actions minutes.

## Responsibility split

### WinInspect public product repository

Owns product behavior and product-release correctness:

- source build and unit/integration tests;
- packaging of the NSIS installer and portable release artifacts;
- product-owned packaged-installer lifecycle proof;
- release subject staging;
- public Foundry trust-envelope generation;
- immutable GitHub Release creation and attestation verification.

A release must fail closed before publication if the packaged product lifecycle fails.

### Public Windows Package Foundry

Owns generic client/distribution mechanics:

- verification of immutable public release identity and hashes;
- native package-manager projection generation;
- WinGet, Scoop, and Chocolatey client lifecycle proof;
- generated public catalog/static human interface;
- generic provenance/trust presentation;
- optional downstream registry mirrors.

Public Foundry code must not contain private evaluator knowledge or private-repository credentials.

### Private Windows Package Foundry

Owns the authority/value plane only:

- product/package eligibility policy;
- specialized validation and evaluator knowledge;
- adversarial/conformance evidence that should not be public;
- approval and promotion decisions;
- construction of a sanitized approved public projection.

The private plane should be executable locally or on a small self-hosted runner. It must not be the location for heavyweight compilation or generic package-manager lifecycle work.

## Zero-budget operating mode

When private GitHub-hosted Actions minutes are unavailable:

1. continue all product build/test/release work in public WinInspect;
2. continue all generic distribution/client lifecycle work in public Foundry;
3. run private authority validators locally from an immutable private-repo commit;
4. preserve local validation evidence with the exact commit and input hashes;
5. do not publish an installable public projection until the private promotion decision is explicitly recorded;
6. when private hosted minutes or a self-hosted runner are available, replay the private validation without changing the approved inputs.

Local validation is a continuity mechanism, not permission to bypass the private authority gate.

## Artifact flow

```text
WinInspect source
      |
      v
public build/test/package/release
      |
      +--> immutable release + hashes + attestations
      |
      v
public Foundry client lifecycle candidates
      |
      +--> WinGet / Scoop / Chocolatey evidence
      |
      v
private authority validation
      |
      +--> sanitized approved public model
      |
      v
public deterministic projection
      |
      +--> catalog JSON / static HTML
      +--> Scoop bucket
      +--> WinGet manifests
      `--> Chocolatey local-feed package
```

No stage may infer approval from the existence of a public artifact alone.

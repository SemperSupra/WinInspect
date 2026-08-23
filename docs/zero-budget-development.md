# Zero-budget development workflow

This repository is public, so standard GitHub-hosted Actions used here do not consume the private Foundry's monthly Actions allowance. Use that separation deliberately.

## Normal development loop

1. Make product changes in WinInspect.
2. Run product build/test/package/lifecycle workflows here.
3. Publish only immutable release subjects that passed product-owned release gates.
4. Let public Windows Package Foundry exercise package-manager-specific client lifecycles against those exact subjects.
5. Let the private Foundry make the eligibility/promotion decision from immutable public evidence.
6. Project only the sanitized approved model back into the public Foundry.

## While private hosted Actions are unavailable

Allowed:

- public WinInspect builds, tests, packaging and release-readiness work;
- public Foundry projection/client work;
- local private validator execution against an exact private commit;
- preparation of private PRs whose correctness can later be replayed on a private or self-hosted runner.

Not allowed:

- treating a public lifecycle pass as private promotion approval;
- copying specialized private evaluator logic into a public repository merely to obtain free CI;
- making the private repository public to gain hosted minutes;
- weakening required private checks so a PR can merge;
- rewriting an immutable release to avoid a failed gate.

## Local private-validation evidence

When a private validator is run locally, preserve at minimum:

- private repository commit SHA;
- validator entrypoint/version;
- public release repository/tag/source SHA;
- exact artifact hashes;
- public lifecycle run identifiers/evidence digests being evaluated;
- UTC timestamp;
- platform/runtime information;
- pass/fail result for every private check.

The evidence should be replayable later with the same inputs. A later hosted/self-hosted run should confirm the result before relying on it as durable CI evidence.

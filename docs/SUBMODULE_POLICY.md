# Submodule Co-Evolution Policy

This document defines the policy for managing Git submodules in this project. We follow a "co-evolution" model where submodules can be modified locally for integration and testing, but synchronization with upstream projects happens strictly through their respective issue trackers.

## Core Rules

1.  **Local Modification is Allowed**: You MAY modify code inside submodules locally to prototype features, fix integration bugs, or perform E2E testing.
2.  **No Direct Upstream Pushes**: You MUST NEVER push any commits directly to the upstream submodule repositories from this project.
3.  **No Forks**: We do not maintain project-specific forks of submodules. We use the official upstream repositories.
4.  **Issues-First Synchronization**: All changes intended for upstream must be submitted as "Issue Packages" (patches and reproduction notes) to the upstream issue tracker.
5.  **Converge on Upstream PRs**: Once the upstream team accepts the changes (via their own PRs and releases), we update our submodule pointer to the new upstream commit and discard our local patches.

## Workflow: Contributing to Upstream

When you have local changes in a submodule that need to go upstream:

1.  **Prototype**: Develop and verify the fix locally within the `tools/<submodule>` directory.
2.  **Export Issue Package**: Use `scripts/submodules/export-issue-package.sh` to generate a package containing patches, diff stats, and a summary template.
3.  **File Upstream Issue**: Open an issue on the upstream GitHub repository. Attach the patches and fill in the summary template (Problem, Reproduction, Acceptance Criteria).
4.  **Wait for Upstream**: The upstream team will review and eventually merge the fix into their main branch.
5.  **Converge**: Use `scripts/submodules/converge-to-upstream.sh` to reset your local working tree to the new upstream version and update the superproject's pointer.

## Guardrails

The following technical measures are in place to prevent accidental violations:

-   **Pre-push Hooks**: A Git hook blocks any `git push` attempt to remotes named "origin" or "upstream" within submodules.
-   **Disabled Push URLs**: Upstream remotes in submodules have their `pushurl` set to `DISABLED`.
-   **Enforcement Script**: Run `scripts/submodules/enforce-policy.sh` to ensure all guardrails are active on your local machine.

## Common Pitfalls

-   **Accidental `git push --recursive`**: The pre-push hook is designed to block this at the submodule level.
-   **Losing local work on `git submodule update`**: Always commit your local submodule changes to a local branch before updating. Use `git stash` if necessary.
-   **Merge Conflicts**: If upstream changes conflict with your local prototype, use standard Git rebasing within the submodule.

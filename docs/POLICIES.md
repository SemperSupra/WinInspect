# Engineering Policies

This project implements two complementary guardrail policies to ensure environment stability, build correctness, and a disciplined upstream contribution workflow.

---

## Policy A: Submodule Co-Evolution

**Goal:** Allow local prototyping and integration testing while maintaining a "pull-only" relationship with upstream dependencies.

### Rules
1.  **Local Edits Allowed:** You MAY modify code inside `external/` submodules for prototyping, bug fixing, and integration testing.
2.  **No Upstream Pushes:** You MUST NEVER push commits directly to upstream submodule repositories.
3.  **No Forks:** We use official upstream URLs. Do not create project-specific forks.
4.  **Convergence:** Local changes are discarded once the upstream project merges the fix and we update our submodule pointer.

### Workflow: Issues-First Synchronization
1.  **Prototype:** Implement the fix locally in `external/<submodule>`.
2.  **Verify:** Run the full smoke test suite (`tools/wbab smoke`) to ensure correctness.
3.  **Export:** Use `scripts/submodules/export-issue-package.sh` to generate a patch set and summary.
4.  **Submit:** Open an issue on the upstream GitHub repository and attach the issue package.
5.  **Converge:** Once upstream merges the fix, use `scripts/submodules/converge-to-upstream.sh` to align with the new official version.

---

## Policy B: Strict Pull-First

**Goal:** Eliminate "Shadow Infrastructure" by ensuring every build uses the canonical organizational images from GHCR.

### The Invariant
Every build runner MUST attempt to `docker pull` the referenced image from the GitHub Container Registry before executing `docker run`.

### The "Escape Hatch" (Infrastructure Changes)
If you are modifying the infrastructure itself (e.g., changing a `Dockerfile` in `external/WineBotAppBuilder`), the "Pull-First" policy will overwrite your local changes with the old image from GHCR.

To bypass this during prototyping:
- Set `ALLOW_LOCAL_BUILD=1` in your environment.
- This tells the build runner to build the image locally from the submodule source instead of pulling.

### Convergence Loop
1.  **Prototyping:** Modify infrastructure code + `ALLOW_LOCAL_BUILD=1`.
2.  **Verification:** Confirm the new environment works as expected.
3.  **Submission:** Submit patches to the upstream infrastructure project.
4.  **Registry Update:** Upstream CI/CD builds and pushes the new image to GHCR.
5.  **Finalization:** Run with `ALLOW_LOCAL_BUILD=0` (default). The runner pulls the new official image containing your changes.

# Dependency Analysis

This document maps the relationships between the WinInspect superproject, its external submodules, and the containerized toolchains that power the build lifecycle.

## 1. Code Dependencies (Submodules)

All external dependencies are centralized in the `external/` directory.

| Component | Path | Upstream URL | Role |
| :--- | :--- | :--- | :--- |
| **WineBot** | `external/WineBot` | `mark-e-deyoung/WineBot` | **Product:** Core runtime environment for Windows automation. |
| **WBAB** | `external/WineBotAppBuilder` | `SemperSupra/WineBotAppBuilder` | **Infrastructure:** Defines the build, package, and test toolchains. |
| **supragoflow** | `external/supragoflow` | `SemperSupra/supragoflow` | **Infrastructure:** Toolchain for building portable Go-based CLIs. |

## 2. Toolchain Dependencies (Images)

Build entry points depend on specific container images pulled from GHCR.

| Entry Point | Image Reference (GHCR) | Defined By | Pull-First Enforced? |
| :--- | :--- | :--- | :--- |
| `tools/wbab build` | `winebotappbuilder-winbuild:v0.3.7` | `external/WineBotAppBuilder` | ✅ Yes |
| `tools/wbab package` | `winebotappbuilder-packager:v0.3.7` | `external/WineBotAppBuilder` | ✅ Yes |
| `tools/wbab sign` | `winebotappbuilder-signer:v0.3.7` | `external/WineBotAppBuilder` | ✅ Yes |
| `tools/wbab lint` | `supragoflow-build:latest` | `external/supragoflow` | ✅ Yes |
| `scripts/smoke.sh` | `winebot:v0.9.5` | `external/WineBot` | ✅ Yes |

## 3. Infrastructure vs. Product

*   **Infrastructure Submodules:** `WineBotAppBuilder` and `supragoflow`. These submodules contain Dockerfiles and build logic. Modifying these typically requires the `ALLOW_LOCAL_BUILD=1` escape hatch to verify changes.
*   **Product Submodules:** `WineBot`. This is the primary runtime. Changes here are verified using the stable, pulled toolchains from the infrastructure submodules.

# WiX/MSI installer bakeoff

This branch exists only to answer whether Windows Installer transactions solve the upgrade-recovery defect demonstrated by the incumbent NSIS path and reproduced by the Inno candidate.

## Evidence required

- Windows Server 2025 x64: native x64 payload, clean/repeat install, A→B, B→A, corrupt-package rejection, clean uninstall.
- Windows 11 ARM64: native ARM64 payload, same lifecycle.
- Wine: x64 MSI clean/repeat install, A→B, B→A, installed CLI smoke, clean uninstall.
- Decisive falsifier on both hosted Windows architectures: hold an exclusive no-share lock on installed `wininspect.exe`, attempt B over accepted A, require a non-zero install result and exact restoration/preservation of every A product hash and A registration.

The MSI major-upgrade removal is scheduled `afterInstallInitialize` so removal of A and installation of B are inside the Windows Installer transaction and rollback can restore A on failure. No custom transaction layer or bootstrapper is part of this rep.

## Decision rule

- If MSI passes the locked-file falsifier on x64 and ARM64 and the x64 MSI lifecycle works under Wine, WiX/MSI becomes the leading backend despite architecture-specific MSI artifacts; a universal Burn wrapper is optional convenience and must separately earn its keep.
- If MSI passes Windows transactional recovery but fails Wine, retain MSI as the Windows backend and compare the cost of an Inno Wine-only projection against relaxing the single-backend preference.
- If MSI also leaves mixed state, do not add a custom transaction framework automatically; revisit versioned side-by-side installation/atomic activation and other established prior art first.

Hosted GitHub Windows results qualify only the exercised package behavior. They do not establish controlled WinBot/native interactive acceptance.

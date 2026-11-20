# Installer backend status

- NSIS incumbent: ordinary lifecycle works, but the historical exclusive-file-lock upgrade can return success with a mixed binary set.
- Inno challenger: Wine and ordinary hosted Windows lifecycle work and one setup EXE selects exact native x64/ARM64 payloads; under the same exclusive-file-lock upgrade it returns nonzero and preserves A registration but still changes other binaries before failing, leaving mixed state.
- WiX/MSI: this branch tests whether Windows Installer rollback preserves exact A state under the same fault without bespoke transaction machinery.

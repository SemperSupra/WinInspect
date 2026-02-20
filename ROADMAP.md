# WinInspect Roadmap & Backlog

This document tracks the long-term evolution of WinInspect as the primary automation engine for WineBot.

## 🚀 Active Development: Observability & Reliability
- **Visual Telemetry Overlay:** Real-time GDI HUD for human supervision.
- **Semantic Action Chains:** Server-side atomic macro execution to bypass latency.

## 📋 Backlog (Prioritized)

### 1. Optical Character Recognition (OCR) 
- **Status:** Deferred (See `.github/ISSUES.md`).
- **Goal:** Enable `screen.findText` and `screen.ocr` to "read" applications that lack UIA/Win32 metadata.
- **Placement:** Investigating Client-side (GUI/CLI) or Plugin-based implementation to keep Core lightweight.

### 3. Virtual Input Driver (Control)
- **Goal:** Bypass "Anti-Automation" and apps using raw/direct input.
- **Tech:** Wine-level input injection or a virtual HID driver.
- **Impact:** Makes WinInspect's input indistinguishable from physical hardware.

### 4. Advanced Hooking (Event Streaming)
- **Goal:** Real-time streaming of keyboard/mouse events from the user to the agent.
- **Tech:** `SetWindowsHookEx` integration in the daemon.
- **Impact:** Enables "Record and Replay" and "Agent Co-Piloting" features.

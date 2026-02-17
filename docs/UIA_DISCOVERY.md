# Wine UIA Discovery and Extension

This document outlines how to identify missing UI Automation (UIA) features in your Wine environment and how to extend Wine's capabilities using standalone artifacts provided in this repository.

## 1. Discovery: Identifying Missing Features

The `tools/survey-uia` tool is designed to probe the Wine environment for specific UIA capabilities.

### Building the Survey Tool

You must build this tool using a Windows toolchain (MSVC or MinGW).

```bash
cd tools/survey-uia
mkdir build
cd build
cmake ..
cmake --build .
```

### Running the Survey

Run the executable inside your Wine environment:

```bash
wine survey_uia.exe > report.json
```

### Interpreting the Report

The tool outputs a JSON report. Key checks include:

- **`CoCreateInstance(CLSID_CUIAutomation)`**: If this fails, the core UIA system is not initialized or `UIAutomationCore.dll` is missing/broken.
- **`GetRootElement`**: If this fails, the desktop root cannot be accessed.
- **`ElementFromHandle`**: If this fails, mapping HWNDs to UIA elements is broken.
- **Patterns**: Checks for support of common patterns (`LegacyIAccessible`, `Invoke`, `Value`).

Example failure:
```json
{
  "name": "CoCreateInstance(CLSID_CUIAutomation)",
  "passed": false,
  "details": "HRESULT: -2147221164"
}
```

## 2. Extension: Layering New Capabilities

If features are missing, you can layer new capabilities into Wine without recompiling Wine itself by creating a **Wine UIA Extension DLL**.

We provide a prototype for such an extension in `tools/wine-uia-extension`.

### The Prototype

The `tools/wine-uia-extension` project contains a scaffold for a COM DLL. This DLL can be used to:

1.  Implement missing COM interfaces (e.g., a custom `IUIAutomation` implementation).
2.  Provide a `IRawElementProviderSimple` for specific window classes.
3.  Hook into the existing system via registry overrides.

### Building the Extension

```bash
cd tools/wine-uia-extension
mkdir build
cd build
cmake ..
cmake --build .
```

This produces `wine_uia_extension.dll`.

### Installing in Wine

To use this extension, you typically register it as a COM server in the Wine prefix:

```bash
wine regsvr32 wine_uia_extension.dll
```

*Note: You may need to update the `extension.cpp` file to implement the actual `DllRegisterServer` logic to write the correct registry keys for the interfaces you are patching.*

### Strategy for Implementation

1.  **Identify the missing Interface/CLSID** from the survey report.
2.  **Implement the Interface** in `extension.cpp`.
3.  **Assign a CLSID** to your implementation.
4.  **Register the DLL** so that applications (like `wininspect`) load your DLL instead of (or in addition to) the system default.

# WinInspect Portable (PortableApps.com)

This directory contains the configuration files for a PortableApps.com
portable application package.

## Structure

```
WinInspectPortable/
  WinInspectPortable.exe          # PortableApps.com Launcher (runtime)
  App/
    AppInfo/
      appinfo.ini                 # application metadata
      launcher.ini                # launch configuration
    WinInspect/
      wininspectd.exe             # daemon
      wininspect.exe              # CLI client
      wininspect-gui.exe          # GUI client
```

## Building the .paf.exe

1. Build WinInspect binaries (CMake Release build)
2. Run `scripts/build-portableapps.sh <version>` which:
   - Stages binaries into this directory structure
   - Downloads the PortableApps.com Launcher binary
   - Runs the PortableApps.com Installer to produce the `.paf.exe`

## Runtime behavior

- `WinInspectPortable.exe` starts `wininspectd.exe` (daemon) in the background
- If command-line parameters are given, it runs `wininspect.exe` with those params
- On exit, the launcher terminates `wininspectd.exe`
- No registry writes, no files left outside the portable directory
- Named pipe `\\.\pipe\wininspectd` is a kernel object, cleaned up when daemon exits

## Download

The PortableApps.com Launcher is freely redistributable:
https://portableapps.com/apps/development/portableapps.com_launcher

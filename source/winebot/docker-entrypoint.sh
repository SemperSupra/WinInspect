#!/bin/sh
# SPDX-License-Identifier: PolyForm-NC-1.0.0
#
# WinInspect Wine test entrypoint.
# Starts Xvfb for display support if available; daemon runs headless either way.

# Start Xvfb on display :99 (best-effort, daemon runs headless)
Xvfb :99 -screen 0 1920x1080x24 -ac 2>/dev/null &
XVPID=$!
sleep 2
if kill -0 $XVPID 2>/dev/null; then
    export DISPLAY=:99
    echo "Xvfb ready on display :99"
else
    echo "Xvfb failed to start, running displayless"
    XVPID=""
fi

# Install VC++ redist if msvcp140.dll is missing (needed for _Throw_Cpp_error)
if [ ! -f /root/.wine/drive_c/windows/system32/msvcp140.dll ]; then
    echo "Installing VC++ 2022 redistributable..."
    if command -v winetricks >/dev/null 2>&1; then
        winetricks -q vcrun2022 2>/dev/null && echo "VC++ redist installed" || echo "VC++ redist install failed"
    else
        echo "winetricks not available, skipping VC++ install"
    fi
fi

# Run the daemon (default: headless mode via CMD)
if [ $# -eq 0 ]; then
    wine wininspectd.exe --headless
else
    "$@"
fi
DAEMON_EXIT=$?

# Cleanup Xvfb
[ -n "$XVPID" ] && kill $XVPID 2>/dev/null
exit $DAEMON_EXIT

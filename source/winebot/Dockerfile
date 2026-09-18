# SPDX-License-Identifier: PolyForm-NC-1.0.0
# Copyright (c) 2026 Mark E. DeYoung
#
# WinInspect Wine test image.
# Builds on WineBot's base image for a full Wine + X11 environment.
# Usage: docker build -t wininspect-wine-test -f winebot/Dockerfile .
#        docker run --rm wininspect-wine-test xvfb-run wine wininspectd.exe --headless

ARG BASE_IMAGE=ghcr.io/sempersupra/winebot-base:base-2026-05-04

FROM ${BASE_IMAGE} AS wine-test

# Install xvfb for headless display + tools for VC++ redist extraction
RUN apt-get update && apt-get install -y --no-install-recommends \
    xvfb x11-utils p7zip-full wget ca-certificates cabextract \
    && rm -rf /var/lib/apt/lists/* && \
    wget -q "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks" -O /usr/local/bin/winetricks && \
    chmod +x /usr/local/bin/winetricks

# Create app directory
RUN mkdir -p /opt/wininspect

# Copy WinInspect binaries (built separately, mounted or COPY'd at build time)
# For CI: use --build-arg from CI artifact path
ARG BINARY_DIR=./build_msvc/Release
COPY ${BINARY_DIR}/wininspectd.exe /opt/wininspect/
COPY ${BINARY_DIR}/wininspect.exe /opt/wininspect/
COPY ${BINARY_DIR}/wininspect-gui.exe /opt/wininspect/

# Entrypoint script: starts Xvfb, waits for readiness, runs daemon
COPY winebot/docker-entrypoint.sh /opt/wininspect/
RUN chmod +x /opt/wininspect/docker-entrypoint.sh

WORKDIR /opt/wininspect

# Ports: 1985 TCP, 8088 HTTP
EXPOSE 1985 8088

# Default: run daemon headless under Xvfb (virtual framebuffer)
ENTRYPOINT ["/opt/wininspect/docker-entrypoint.sh"]
CMD ["wine", "wininspectd.exe", "--headless"]

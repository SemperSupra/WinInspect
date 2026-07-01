# WinInspect — Formal Methods Targets
# Requires: Java 21+ (TLC), tla2tools.jar in tools/

TLC_JAR := tools/tla2tools.jar
TLC_FLAGS := -workers 4 -XX:+UseParallelGC

.PHONY: formal formal-fast formal-full formal-clean test

# ── Fast checks (every PR) ──────────────────────────────────────────────────

formal-fast: formal-handshake-fast formal-v2-fast

formal-handshake-fast:
	cd formal/tla && java $(TLC_FLAGS) -cp ../../$(TLC_JAR) tlc2.TLC \
	  WinInspect_Handshake.tla -config handshake_tlc.cfg -depth 6 2>&1 | tail -10

formal-v2-fast:
	cd formal/tla && java $(TLC_FLAGS) -cp ../../$(TLC_JAR) tlc2.TLC \
	  WinInspect_v2.tla -config v2_tlc.cfg -depth 4 2>&1 | tail -10

# ── Full checks (nightly / pre-release) ─────────────────────────────────────

formal-full: formal-handshake-full formal-v2-full

formal-handshake-full:
	cd formal/tla && java $(TLC_FLAGS) -cp ../../$(TLC_JAR) tlc2.TLC \
	  WinInspect_Handshake.tla -config handshake_tlc.cfg -depth 10 2>&1 | tail -10

formal-v2-full:
	cd formal/tla && java $(TLC_FLAGS) -cp ../../$(TLC_JAR) tlc2.TLC \
	  WinInspect_v2.tla -depth 6 2>&1 | tail -10

# ── Cleanup ──────────────────────────────────────────────────────────────────

formal-clean:
	rm -f formal/tla/*.states formal/tla/*.dot formal/tla/*.tla.*

# ── Alias ────────────────────────────────────────────────────────────────────

formal: formal-fast

# ── Tests ────────────────────────────────────────────────────────────────────

test:
	ctest --test-dir build -C Release --output-on-failure

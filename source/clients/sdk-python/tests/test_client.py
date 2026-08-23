"""Tests for the WinInspect Python Agent SDK.

Requires daemon running on localhost:1985.
Run: cd clients/sdk-python && python -m pytest tests/
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
from wininspect.client import Client, WinInspectError


@pytest.fixture
def client():
    c = Client(transport="tcp", host="127.0.0.1", port=1985)
    yield c
    c.close()


# ── Connection ──────────────────────────────────────────────────────────────

class TestClientConnection:
    def test_create_and_close(self):
        c = Client(transport="tcp", host="127.0.0.1", port=1985)
        assert c._sock is not None
        c.close()
        assert c._sock is None

    def test_double_close(self):
        c = Client(transport="tcp", host="127.0.0.1", port=1985)
        c.close()
        c.close()

    def test_connect_refused(self):
        with pytest.raises((ConnectionRefusedError, ConnectionError)):
            Client(transport="tcp", host="127.0.0.1", port=19999)

    def test_context_manager(self):
        with Client(transport="tcp", host="127.0.0.1", port=1985) as c:
            assert c._sock is not None


# ── Daemon Info ─────────────────────────────────────────────────────────────

class TestDaemonInfo:
    def test_health(self, client):
        resp = client.health()
        assert resp.get("ok") is True
        result = resp.get("result", {})
        assert "os" in result
        assert "features" in result
        features = result["features"]
        for cap in ("clipboard", "uia", "input_injection",
                    "process_memory", "registry_write",
                    "window_highlight", "pipe_available"):
            assert cap in features, f"missing: {cap}"

    def test_identity(self, client):
        resp = client.identity()
        assert resp.get("ok") is True
        result = resp.get("result", {})
        assert "uuid" in result
        assert "name" in result

    def test_capabilities(self, client):
        resp = client.call("daemon.capabilities")
        assert resp.get("ok") is True
        result = resp.get("result", {})
        assert "features" in result
        assert len(result["features"]) >= 8


# ── Error Handling ─────────────────────────────────────────────────────────

class TestErrorHandling:
    def test_bad_method(self, client):
        with pytest.raises(WinInspectError):
            client.call("nonexistent.method")

    def test_bad_params(self, client):
        with pytest.raises(WinInspectError):
            client.call("window.getInfo")


# ── Window Methods ──────────────────────────────────────────────────────────

class TestWindowMethods:
    def test_list_top(self, client):
        resp = client.call("window.listTop")
        assert resp.get("ok") is True
        assert isinstance(resp.get("result"), list)

    def test_desktop_info(self, client):
        resp = client.call("screen.desktopInfo")
        assert resp.get("ok") is True
        result = resp.get("result", {})
        assert "width" in result
        assert "height" in result


# ── Session Lifecycle ───────────────────────────────────────────────────────

class TestSessionLifecycle:
    def test_subscribe_unsubscribe(self, client):
        resp = client.call("events.subscribe", session_id="pytest-session")
        assert resp.get("ok") is True
        result = resp.get("result", {})
        assert result.get("subscribed") is True
        assert "snapshot_id" in result

        resp = client.call("events.unsubscribe", session_id="pytest-session")
        assert resp.get("ok") is True


if __name__ == "__main__":
    pytest.main([__file__, "-v"])

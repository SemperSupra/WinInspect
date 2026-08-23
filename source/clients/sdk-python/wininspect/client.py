"""WinInspect RPC client — TCP, HTTP, and WebSocket transports."""

from __future__ import annotations

import json
import socket
import struct
import base64
import threading
import logging
from enum import Enum
from typing import Any, Optional

logger = logging.getLogger(__name__)


class ControllerType(str, Enum):
    """Control state types matching the daemon's ControllerType."""
    NONE = "none"
    HUMAN = "human"
    AGENT = "agent"
    SCRIPT = "script"


class WinInspectError(Exception):
    """Raised when the daemon returns an error response."""
    def __init__(self, code: str, message: str):
        self.code = code
        self.message = message
        super().__init__(f"[{code}] {message}")


class Client:
    """Client for a WinInspect daemon.

    Supports TCP (native protocol), HTTP (REST API),
    and WebSocket (event streaming) transports.

    Args:
        transport: One of "tcp", "http", "ws"
        host: Daemon hostname or IP
        port: Daemon port (default: 1985 for TCP, 8088 for HTTP)
        token: Bearer token for HTTP auth
        connect_timeout: Connection timeout in seconds
    """

    def __init__(
        self,
        transport: str = "tcp",
        host: str = "127.0.0.1",
        port: Optional[int] = None,
        token: str = "",
        connect_timeout: float = 5.0,
    ):
        self.transport = transport
        self.host = host
        self.token = token
        self.connect_timeout = connect_timeout
        self._sock: Optional[socket.socket] = None
        self._req_id = 0
        self._lock = threading.Lock()

        if port is None:
            self.port = {"tcp": 1985, "http": 8088, "ws": 8088}.get(transport, 1985)
        else:
            self.port = port

        self._connect()

    # ── Connection Management ──────────────────────────────────────────────

    def _connect(self) -> None:
        """Establish connection to the daemon."""
        if self.transport == "tcp":
            self._connect_tcp()
        elif self.transport in ("http", "ws"):
            self._http_port = self.port
        else:
            raise ValueError(f"Unknown transport: {self.transport}")

    def _connect_tcp(self) -> None:
        """TCP transport: connect and perform handshake."""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(self.connect_timeout)
        self._sock.connect((self.host, self.port))
        self._sock.settimeout(None)

        # Handshake: receive challenge
        raw_len = self._recv_all(4)
        if not raw_len:
            raise ConnectionError("Failed to read handshake length")
        msg_len = struct.unpack("!I", raw_len)[0]
        challenge_raw = self._recv_all(msg_len)
        challenge = json.loads(challenge_raw)
        logger.info("Connected to %s (%s)", challenge.get("name", ""), challenge.get("uuid", ""))

    def _recv_all(self, n: int) -> bytes:
        """Read exactly n bytes from the socket."""
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Connection closed")
            buf.extend(chunk)
        return bytes(buf)

    def close(self) -> None:
        """Close the connection."""
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def __enter__(self) -> "Client":
        return self

    def __exit__(self, *args) -> None:
        self.close()

    # ── RPC Calls ──────────────────────────────────────────────────────────

    def call(self, method: str, **params: Any) -> dict:
        """Call an RPC method on the daemon.

        Args:
            method: The RPC method name (e.g. "window.listTop")
            **params: Method parameters as keyword arguments

        Returns:
            The response dict with keys "ok", "result", etc.

        Raises:
            WinInspectError: If the daemon returns an error response
            ConnectionError: If the connection is lost
        """
        with self._lock:
            self._req_id += 1
            req_id = f"py-{self._req_id}"

        if self.transport == "tcp":
            return self._call_tcp(method, params, req_id)
        elif self.transport == "http":
            return self._call_http(method, params, req_id)
        else:
            raise ValueError(f"Cannot make RPC calls over {self.transport} transport")

    def _call_tcp(self, method: str, params: dict, req_id: str) -> dict:
        """TCP RPC: frame-level protocol."""
        if not self._sock:
            raise ConnectionError("Not connected")

        request = json.dumps({
            "id": req_id,
            "method": method,
            "params": params,
            "protocol_version": "0.3.0",
        })
        frame = struct.pack("!I", len(request)) + request.encode()
        self._sock.sendall(frame)

        # Read response
        raw_len = self._recv_all(4)
        resp_len = struct.unpack("!I", raw_len)[0]
        resp_raw = self._recv_all(resp_len)
        response = json.loads(resp_raw)

        if not response.get("ok"):
            raise WinInspectError(
                response.get("error_code", "E_UNKNOWN"),
                response.get("error_message", "Unknown error"),
            )
        return response

    def _call_http(self, method: str, params: dict, req_id: str) -> dict:
        """HTTP RPC: REST-style API call."""
        import urllib.request
        import urllib.error

        # Map method to HTTP endpoint
        method_to_path = {
            "window.listTop": "/api/v1/windows",
            "screen.capture": "/api/v1/capture",
            "input.mouseClick": "/api/v1/click",
            "input.text": "/api/v1/type",
            "input.hotkey": "/api/v1/hotkey",
            "daemon.health": "/api/v1/health",
            "daemon.identity": "/api/v1/identity",
            "daemon.capabilities": "/api/v1/capabilities",
            "process.list": "/api/v1/processes",
            "process.execute": "/api/v1/exec",
        }

        path = method_to_path.get(method, f"/api/v1/rpc/{method}")
        url = f"http://{self.host}:{self.port}{path}"

        is_post = method in (
            "screen.capture", "input.mouseClick", "input.text",
            "input.hotkey", "process.execute",
        )

        data = json.dumps(params).encode() if is_post and params else None
        req = urllib.request.Request(url, data=data, method="POST" if is_post else "GET")
        req.add_header("Content-Type", "application/json")
        if self.token:
            req.add_header("Authorization", f"Bearer {self.token}")

        try:
            with urllib.request.urlopen(req, timeout=self.connect_timeout) as resp:
                response = json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            raise WinInspectError(f"HTTP_{e.code}", str(e))
        except Exception as e:
            raise ConnectionError(f"HTTP request failed: {e}")

        if not response.get("ok"):
            raise WinInspectError(
                response.get("error_code", "E_UNKNOWN"),
                response.get("error_message", "Unknown error"),
            )
        return response

    # ── Convenience Methods ────────────────────────────────────────────────

    def list_windows(self) -> list:
        """List all top-level windows."""
        return self.call("window.listTop").get("result", [])

    def capture_screen(self, left=0, top=0, right=1920, bottom=1080) -> bytes:
        """Capture the screen and return raw BMP bytes."""
        result = self.call("screen.capture", left=left, top=top, right=right, bottom=bottom)
        data_b64 = result.get("result", {}).get("data_b64", "")
        if data_b64:
            return base64.b64decode(data_b64)
        return b""

    def click(self, x: int, y: int, button: int = 0) -> dict:
        """Click at screen coordinates."""
        return self.call("input.mouseClick", x=x, y=y, button=button)

    def type_text(self, text: str) -> dict:
        """Type text."""
        return self.call("input.text", text=text)

    def hotkey(self, keys: str) -> dict:
        """Send a hotkey combination."""
        return self.call("input.hotkey", keys=keys)

    def health(self) -> dict:
        """Get daemon health status."""
        return self.call("daemon.health")

    def identity(self) -> dict:
        """Get daemon identity."""
        return self.call("daemon.identity")

    # ── Control Methods ──────────────────────────────────────────────────

    def take_control(self, controller: ControllerType, id: str = "") -> dict:
        """Take control of the daemon."""
        return self.call("control.take", controller=controller.value, id=id)

    def release_control(self, controller: ControllerType, id: str = "") -> dict:
        """Release control."""
        return self.call("control.release", controller=controller.value, id=id)

    def control_status(self) -> dict:
        """Get current control state."""
        return self.call("control.status")

    # ── Session Recording ────────────────────────────────────────────────

    def start_recording(self, output_path: str = "session.wisession",
                        interval_ms: int = 1000, max_frames: int = 0) -> dict:
        """Start a daemon-side session recording."""
        return self.call(
            "daemon.session.startRecording",
            output_path=output_path,
            interval_ms=interval_ms,
            max_frames=max_frames,
        )

    def stop_recording(self) -> dict:
        """Stop the current session recording."""
        return self.call("daemon.session.stopRecording")

    def recording_status(self) -> dict:
        """Get recording status."""
        return self.call("daemon.recordingStatus")

    # ── Credential Management ────────────────────────────────────────────

    def credential_store(self, target: str, username: str, password: str) -> dict:
        """Store a credential."""
        return self.call("credential.store", target=target, username=username, password=password)

    def credential_retrieve(self, target: str) -> dict:
        """Retrieve a credential."""
        return self.call("credential.retrieve", target=target)

    def credential_delete(self, target: str) -> dict:
        """Delete a credential."""
        return self.call("credential.delete", target=target)

    def credential_list(self) -> dict:
        """List all stored credential targets."""
        return self.call("credential.list")

    def credential_generate(self, length: int = 32) -> dict:
        """Generate a random password."""
        return self.call("credential.generate", length=length)

    # ── WebSocket Events ────────────────────────────────────────────────

    def events(self, wait_ms: float = 500) -> Any:
        """Generator that yields daemon events via WebSocket.

        Usage:
            for event in client.events():
                print(event)

        Yields:
            Event dicts from the daemon.
        """
        try:
            import websocket
        except ImportError:
            raise ImportError("WebSocket support requires: pip install websocket-client")

        ws_url = f"ws://{self.host}:{self.port}/api/v1/events"
        ws = websocket.create_connection(ws_url, timeout=self.connect_timeout)

        try:
            while True:
                frame = ws.recv()
                if isinstance(frame, bytes):
                    yield json.loads(frame.decode())
                else:
                    yield json.loads(frame)
        except Exception as e:
            logger.warning("WebSocket closed: %s", e)
        finally:
            ws.close()

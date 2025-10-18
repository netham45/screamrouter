"""Minimal VNC client for sending key events without third-party dependencies."""

from __future__ import annotations

import socket
import struct
from dataclasses import dataclass


@dataclass
class SimpleVNCClient:
    """Very small subset of the RFB protocol sufficient for key events."""

    host: str
    port: int = 5900
    timeout: float | None = 5.0

    def __post_init__(self) -> None:
        self._sock: socket.socket | None = None

    # ---- Lifecycle helpers -------------------------------------------------
    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._perform_handshake()

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    # ---- Public API --------------------------------------------------------
    def send_key(self, keysym: int, pressed: bool) -> None:
        if self._sock is None:
            raise RuntimeError("VNC client is not connected")
        message = struct.pack("!BBxxI", 4, 1 if pressed else 0, keysym)
        self._sock.sendall(message)

    # ---- Protocol helpers --------------------------------------------------
    def _perform_handshake(self) -> None:
        assert self._sock is not None
        server_version = self._recv_exact(12).decode("ascii")
        if not server_version.startswith("RFB "):
            raise RuntimeError(f"Invalid VNC server greeting: {server_version!r}")
        self._sock.sendall(server_version.encode("ascii"))

        major = int(server_version[4:7])
        if major < 3:
            raise RuntimeError(f"Unsupported RFB major version: {major}")

        if major == 3 and int(server_version[8:11]) <= 3:
            self._handle_security_v33()
        else:
            self._handle_security_v38()

        # ClientInit: request to share the desktop
        self._sock.sendall(b"\x01")
        # ServerInit payload – width, height, pixel format, name length + name
        self._consume_server_init()

    def _handle_security_v33(self) -> None:
        security_type = struct.unpack("!I", self._recv_exact(4))[0]
        if security_type == 0:
            self._raise_security_failure()
        if security_type != 1:
            raise RuntimeError(f"Unsupported VNC security type {security_type}")

    def _handle_security_v38(self) -> None:
        num_types = self._recv_exact(1)[0]
        if num_types == 0:
            self._raise_security_failure()
        security_types = self._recv_exact(num_types)
        if 1 not in security_types:
            raise RuntimeError("VNC server does not offer 'None' security type")
        # Select security type 1 (None)
        self._sock.sendall(b"\x01")
        security_result = struct.unpack("!I", self._recv_exact(4))[0]
        if security_result != 0:
            self._raise_security_failure()

    def _consume_server_init(self) -> None:
        # width + height + pixel format (16 bytes) + name length + name
        _ = self._recv_exact(24)  # width(2)+height(2)+pixfmt(16)+name length(4)
        name_length = struct.unpack("!I", _[-4:])[0]
        if name_length:
            self._recv_exact(name_length)

    def _recv_exact(self, size: int) -> bytes:
        assert self._sock is not None
        data = b""
        while len(data) < size:
            chunk = self._sock.recv(size - len(data))
            if not chunk:
                raise RuntimeError("Unexpected EOF from VNC server")
            data += chunk
        return data

    def _raise_security_failure(self) -> None:
        reason_length = struct.unpack("!I", self._recv_exact(4))[0]
        reason = self._recv_exact(reason_length).decode("utf-8", errors="replace")
        raise RuntimeError(f"VNC security handshake failed: {reason}")


__all__ = ["SimpleVNCClient"]


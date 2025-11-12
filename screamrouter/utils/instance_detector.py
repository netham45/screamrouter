"""Helper utilities for detecting an already running ScreamRouter instance."""
from __future__ import annotations

import json
import socket
import ssl
from dataclasses import dataclass
from typing import Iterable
from urllib import error, request


@dataclass(frozen=True)
class InstanceDetectionResult:
    """Represents a detected ScreamRouter instance."""

    api_url: str
    ui_url: str


def _normalize_host(host: str) -> str:
    """Wrap IPv6 hosts with brackets to produce a valid URL netloc."""
    if ":" in host and not host.startswith("["):
        return f"[{host}]"
    return host


def _build_netloc(host: str, port: int) -> str:
    host = _normalize_host(host.strip())
    if port in (80, 443):
        return host
    return f"{host}:{port}"


def _candidate_hosts(preferred_host: str | None) -> list[str]:
    """Generate a list of hosts to probe, avoiding duplicates while preserving order."""
    hosts: list[str] = []
    if preferred_host and preferred_host not in {"0.0.0.0", "::", "::0"}:
        hosts.append(preferred_host)

    hosts.extend(["localhost", "127.0.0.1"])

    try:
        hostname = socket.gethostname()
        if hostname:
            hosts.append(hostname)
    except OSError:
        pass

    try:
        fqdn = socket.getfqdn()
        if fqdn and fqdn not in hosts:
            hosts.append(fqdn)
    except OSError:
        pass

    # Drop duplicates while preserving order
    seen: set[str] = set()
    unique_hosts: list[str] = []
    for host in hosts:
        if host not in seen:
            unique_hosts.append(host)
            seen.add(host)
    return unique_hosts


def detect_running_instance(
    api_host: str | None,
    api_port: int,
    timeout: float = 1.5,
    extra_hosts: Iterable[str] | None = None,
) -> InstanceDetectionResult | None:
    """
    Attempt to detect an already-running ScreamRouter instance by polling the HTTPS API.

    Args:
        api_host: Preferred API host (can be 0.0.0.0 when binding all interfaces).
        api_port: Port ScreamRouter listens on.
        timeout: Per-connection timeout in seconds.
        extra_hosts: Optional additional hosts/IPs to probe first.

    Returns:
        InstanceDetectionResult if the API responds successfully, None otherwise.
    """
    hosts: list[str] = []
    if extra_hosts:
        hosts.extend(extra_hosts)
    hosts.extend(_candidate_hosts(api_host))

    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    for host in hosts:
        netloc = _build_netloc(host, api_port)
        api_url = f"https://{netloc}/api/system/info"
        req = request.Request(api_url, headers={"Accept": "application/json"})
        try:
            with request.urlopen(req, timeout=timeout, context=context) as resp:
                if resp.status != 200:
                    continue
                data = json.load(resp)
        except (error.URLError, error.HTTPError, TimeoutError, ValueError, ssl.SSLError):
            continue

        if isinstance(data, dict) and "hostname" in data:
            ui_url = f"https://{netloc}/"
            return InstanceDetectionResult(api_url=api_url, ui_url=ui_url)

    return None

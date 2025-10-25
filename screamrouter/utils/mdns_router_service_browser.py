"""Helpers for discovering ScreamRouter control-plane services over mDNS."""
from __future__ import annotations

import socket
import threading
import time
from typing import Dict, List, Optional

from zeroconf import ServiceBrowser, ServiceInfo, ServiceListener, Zeroconf

SERVICE_TYPE = "_screamrouter._tcp.local."


def _normalize_properties(properties: Optional[Dict[object, object]]) -> Dict[str, str]:
    if not properties:
        return {}
    normalized: Dict[str, str] = {}
    for raw_key, raw_value in properties.items():
        key = raw_key.decode("utf-8", errors="ignore") if isinstance(raw_key, (bytes, bytearray)) else str(raw_key)
        if isinstance(raw_value, (bytes, bytearray)):
            value = raw_value.decode("utf-8", errors="ignore")
        elif isinstance(raw_value, (list, tuple)):
            value = ",".join(
                item.decode("utf-8", errors="ignore") if isinstance(item, (bytes, bytearray)) else str(item)
                for item in raw_value
            )
        else:
            value = str(raw_value)
        normalized[key] = value
    return normalized


def _service_info_to_payload(info: ServiceInfo) -> Dict[str, object]:
    addresses: List[str]
    if hasattr(info, "parsed_addresses"):
        addresses = list(info.parsed_addresses())  # type: ignore[attr-defined]
    else:
        addresses = [socket.inet_ntoa(addr) for addr in info.addresses] if info.addresses else []

    return {
        "name": info.name,
        "host": (info.server or "").rstrip("."),
        "port": info.port,
        "addresses": addresses,
        "properties": _normalize_properties(getattr(info, "properties", {})),
        "priority": info.priority,
        "weight": info.weight,
    }


class _RouterServiceListener(ServiceListener):
    """Collects `_screamrouter._tcp` service announcements."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._services: Dict[str, Dict[str, object]] = {}

    def _record(self, info: Optional[ServiceInfo]) -> None:
        if not info:
            return
        payload = _service_info_to_payload(info)
        with self._lock:
            self._services[payload["name"]] = payload

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:  # noqa: D401
        self._record(zc.get_service_info(type_, name, timeout=3000))

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:  # noqa: D401
        self.add_service(zc, type_, name)

    def remove_service(self, _zc: Zeroconf, _type: str, name: str) -> None:  # noqa: D401
        with self._lock:
            self._services.pop(name, None)

    def snapshot(self) -> List[Dict[str, object]]:
        with self._lock:
            return list(self._services.values())


def discover_router_services(timeout: float = 2.0, service_type: str = SERVICE_TYPE) -> List[Dict[str, object]]:
    """Actively browse for `_screamrouter._tcp` services for ``timeout`` seconds."""
    if timeout <= 0:
        return []

    zeroconf = Zeroconf()
    listener = _RouterServiceListener()
    browser = ServiceBrowser(zeroconf, service_type, listener)
    try:
        time.sleep(timeout)
    finally:
        browser.cancel()
        zeroconf.close()
    return listener.snapshot()

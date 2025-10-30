"""mDNS advertiser for the ScreamRouter control plane."""
from __future__ import annotations

import logging
import socket
import threading
import uuid
from typing import Dict, Optional

from zeroconf import IPVersion, ServiceInfo, Zeroconf

from screamrouter.preferences.preferences_manager import (
    PreferencesManager,
    PreferencesManagerError,
)

try:
    from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
except ImportError:  # pragma: no cover - fallback outside main runtime
    logging.basicConfig(level=logging.INFO)
    get_logger = logging.getLogger

try:
    import screamrouter  # pylint: disable=wrong-import-position
except ImportError:  # pragma: no cover - during setup
    screamrouter = None

logger = get_logger(__name__)


class RouterServiceAdvertiser(threading.Thread):
    """Advertises the UI/API endpoint via mDNS (_screamrouter._tcp)."""

    def __init__(
        self,
        port: int,
        *,
        site_path: str = "/site",
        api_prefix: str = "/api",
        instance_name: Optional[str] = None,
        additional_txt: Optional[Dict[str, str]] = None,
    ) -> None:
        super().__init__(name="HTTPSAdvertiserThread", daemon=True)
        self.port = port
        self.site_path = site_path if site_path.startswith("/") else f"/{site_path}"
        self.api_prefix = api_prefix if api_prefix.startswith("/") else f"/{api_prefix}"
        self._explicit_instance_name = instance_name
        self.additional_txt = additional_txt or {}

        self.service_type = "_screamrouter._tcp.local."
        self.mac_address = self._get_mac_address()
        self.instance_uuid = self._load_or_create_instance_uuid()
        self.hostname = self._format_hostname()
        self.instance_name = self._build_instance_name()
        self.service_name = f"{self.instance_name}.{self.service_type}"

        self.zeroconf: Optional[Zeroconf] = None
        self.service_info: Optional[ServiceInfo] = None
        self._should_stop = threading.Event()

        logger.info(
            "RouterServiceAdvertiser initialized: name=%s port=%s site_path=%s api_prefix=%s",
            self.instance_name,
            self.port,
            self.site_path,
            self.api_prefix,
        )

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def _build_instance_name(self) -> str:
        if self._explicit_instance_name:
            return self._explicit_instance_name
        # Keep consistent naming with the Scream advertiser
        return self.hostname

    def _load_or_create_instance_uuid(self) -> str:
        manager: Optional[PreferencesManager] = None
        try:
            manager = PreferencesManager()
        except PreferencesManagerError as exc:
            logger.warning("Unable to initialise preferences manager for router advertiser UUID: %s", exc)

        stored_uuid: Optional[str] = None
        if manager:
            try:
                preferences = manager.get_preferences()
                stored_uuid = getattr(preferences, "mdns_router_uuid", None)
            except PreferencesManagerError as exc:
                logger.warning("Failed to load persisted router advertiser UUID: %s", exc)
            except Exception as exc:
                logger.warning("Unexpected error reading router advertiser UUID from preferences: %s", exc)

        if stored_uuid:
            return stored_uuid

        generated_uuid = self._generate_instance_uuid()

        if manager:
            try:
                manager.update_preferences({"mdns_router_uuid": generated_uuid})
            except PreferencesManagerError as exc:
                logger.warning("Unable to persist router advertiser UUID: %s", exc)
            except Exception as exc:
                logger.warning("Unexpected error persisting router advertiser UUID: %s", exc)

        return generated_uuid

    def _generate_instance_uuid(self) -> str:
        mac_hex = (self.mac_address or "").replace(":", "")
        if mac_hex and mac_hex != "000000000000":
            try:
                return str(uuid.uuid5(uuid.NAMESPACE_DNS, f"screamrouter-router-{mac_hex.upper()}"))
            except ValueError as exc:
                logger.warning("Failed deterministic router UUID generation; falling back to random UUID: %s", exc)
        return str(uuid.uuid4())

    @staticmethod
    def _get_mac_address() -> str:
        mac = uuid.getnode()
        try:
            mac_hex = f"{mac:012X}"
            return ":".join(mac_hex[i : i + 2] for i in range(0, 12, 2))
        except Exception:  # pragma: no cover - extremely unlikely
            return "00:00:00:00:00:00"

    def _format_hostname(self) -> str:
        """Prefer the system hostname; fall back to the MAC-derived name."""
        try:
            system_hostname = socket.gethostname().strip()
        except Exception:  # pragma: no cover - platform specific
            system_hostname = ""

        def _sanitize(name: str) -> str:
            cleaned = ''.join(c if c.isalnum() or c in ('-', '_') else '-' for c in name)
            return cleaned.strip('-_')

        candidate = _sanitize(system_hostname)
        if candidate:
            return candidate

        suffix = self.mac_address.replace(":", "")[-6:] if self.mac_address else "000000"
        return f"Screamrouter-{suffix}"

    @staticmethod
    def _get_local_ip() -> str:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.connect(("8.8.8.8", 80))
                return sock.getsockname()[0]
        except Exception:
            return "127.0.0.1"

    def _build_txt_records(self) -> Dict[str, str]:
        version = getattr(screamrouter, "__version__", "unknown") if screamrouter else "unknown"
        base_txt = {
            "scheme": "https",
            "port": str(self.port),
            "site": self.site_path,
            "api": self.api_prefix,
            "hostname": self.hostname,
            "uuid": self.instance_uuid,
            "version": version,
        }
        if self.site_path and self.site_path not in ("/", ""):
            base_txt["path"] = self.site_path
        base_txt.update({str(k): str(v) for k, v in self.additional_txt.items() if v is not None})
        return base_txt

    def _create_service_info(self) -> ServiceInfo:
        ip = self._get_local_ip()
        properties = self._build_txt_records()
        logger.debug("HTTPSAdvertiser properties: ip=%s properties=%s", ip, properties)
        return ServiceInfo(
            type_=self.service_type,
            name=self.service_name,
            addresses=[socket.inet_aton(ip)],
            port=self.port,
            properties=properties,
            server=f"{self.hostname}.local.",
        )

    # ------------------------------------------------------------------
    # Thread lifecycle
    # ------------------------------------------------------------------
    def run(self) -> None:  # pragma: no cover - integration behaviour
        logger.info("RouterServiceAdvertiser thread starting...")
        try:
            self.zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
            self.service_info = self._create_service_info()
            logger.info("Registering ScreamRouter service: %s on port %s", self.service_name, self.port)
            self.zeroconf.register_service(self.service_info, allow_name_change=True)

            while not self._should_stop.wait(timeout=5.0):
                pass
        except Exception as exc:  # pylint: disable=broad-except
            logger.exception("RouterServiceAdvertiser encountered an error: %s", exc)
        finally:
            if self.zeroconf:
                try:
                    if self.service_info:
                        logger.info("Unregistering ScreamRouter service")
                        self.zeroconf.unregister_service(self.service_info)
                finally:
                    self.zeroconf.close()
            self.zeroconf = None
            self.service_info = None
            logger.info("RouterServiceAdvertiser thread finished")

    def stop(self) -> None:
        logger.info("Stopping RouterServiceAdvertiser...")
        self._should_stop.set()
        if self.is_alive():
            self.join(timeout=10)
            if self.is_alive():
                logger.warning("RouterServiceAdvertiser thread did not stop cleanly")

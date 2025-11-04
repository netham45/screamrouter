"""API endpoints for exporting raw PCM from the timeshift buffer."""
from __future__ import annotations

import re
import time

from typing import Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import Response

from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)


class APITimeshift:
    """Expose timeshift PCM dumps over HTTP."""

    def __init__(self, app: FastAPI, configuration_manager: ConfigurationManager):
        self._app = app
        self._configuration_manager = configuration_manager

        self._app.add_api_route(
            "/api/timeshift/{source_tag}",
            self.export_timeshift_pcm,
            methods=["GET"],
            tags=["Timeshift"],
        )

    async def export_timeshift_pcm(
        self,
        source_tag: str,
        lookback_seconds: float = Query(
            default=300.0,
            ge=0.1,
            le=600.0,
            description="How far back in seconds to export from the timeshift buffer.",
        ),
    ) -> Response:
        """Return a binary PCM dump for the requested source."""
        audio_manager = self._configuration_manager.cpp_audio_manager
        if not audio_manager:
            raise HTTPException(status_code=503, detail="C++ audio manager not available")

        export = audio_manager.export_timeshift_buffer(source_tag, lookback_seconds)

        if not export:

            resolved_tag = self._resolve_source_tag_via_configuration(source_tag)
            if resolved_tag and resolved_tag != source_tag:
                logger.debug(
                    "Timeshift export: resolved '%s' via configuration -> '%s'",
                    source_tag,
                    resolved_tag,
                )
                export = audio_manager.export_timeshift_buffer(resolved_tag, lookback_seconds)

        if not export:
            logger.debug(
                "No timeshift data available for source %s (lookback=%.2fs)",
                source_tag,
                lookback_seconds,
            )
            raise HTTPException(
                status_code=404,
                detail="No PCM data found for the requested source/tag",
            )

        pcm_bytes: bytes = export.pcm_data
        if not pcm_bytes:
            raise HTTPException(
                status_code=404,
                detail="No PCM data found for the requested source/tag",
            )

        headers = {
            "X-Audio-Sample-Rate": str(export.sample_rate),
            "X-Audio-Channels": str(export.channels),
            "X-Audio-Bit-Depth": str(export.bit_depth),
            "X-Audio-Duration-Seconds": f"{export.duration_seconds:.6f}",
            "X-Audio-Earliest-Packet-Age-Seconds": f"{export.earliest_packet_age_seconds:.6f}",
            "X-Audio-Latest-Packet-Age-Seconds": f"{export.latest_packet_age_seconds:.6f}",
            "X-Audio-Lookback-Seconds": f"{export.lookback_seconds_requested:.6f}",
        }

        safe_tag = _sanitize_filename_fragment(source_tag)
        timestamp = int(time.time())
        headers["Content-Disposition"] = (
            f'attachment; filename="{safe_tag}_{timestamp}.pcm"'
        )

        return Response(
            content=pcm_bytes,
            media_type="application/octet-stream",
            headers=headers,
        )

    def _resolve_source_tag_via_configuration(self, candidate: str) -> Optional[str]:
        """
        Try to map a human-friendly identifier (name, config_id, IP) to the
        concrete stream tag used inside the timeshift buffer.
        """
        if not candidate or not self._configuration_manager:
            return None

        try:
            sources = list(getattr(self._configuration_manager, "source_descriptions", []))
        except Exception:  # pylint: disable=broad-except
            return None

        candidate = candidate.strip()
        normalized = candidate.lower()

        # Direct match on existing tag or IP
        for desc in sources:
            tag = getattr(desc, "tag", None)
            if tag and tag == candidate:
                return tag
            ip = getattr(desc, "ip", None)
            if ip and str(ip) == candidate:
                return str(ip)

        # Match by friendly name
        for desc in sources:
            name = getattr(desc, "name", "")
            if name and name.lower() == normalized:
                tag = getattr(desc, "tag", None)
                if tag:
                    return tag
                ip = getattr(desc, "ip", None)
                if ip:
                    return str(ip)

        # Match by configuration ID
        for desc in sources:
            config_id = getattr(desc, "config_id", None)
            if config_id and str(config_id) == candidate:
                tag = getattr(desc, "tag", None)
                if tag:
                    return tag
                ip = getattr(desc, "ip", None)
                if ip:
                    return str(ip)

        return None


_FILENAME_CLEAN_PATTERN = re.compile(r"[^A-Za-z0-9_.-]+")


def _sanitize_filename_fragment(source_tag: str) -> str:
    """Return a filesystem-friendly representation of the source tag."""
    cleaned = _FILENAME_CLEAN_PATTERN.sub("_", source_tag.strip())
    return cleaned or "timeshift"

"""
Lightweight helper for downloading PCM dumps from the timeshift buffer API.

This client wraps the `/api/timeshift/{source_tag}` endpoint that exposes the
per-source rolling buffer as a contiguous PCM stream.  Use it from scripts or
CLI tooling to archive or post-process recent audio without touching the UI.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Optional
from urllib.parse import quote

import httpx


_HEADER_SAMPLE_RATE = "x-audio-sample-rate"
_HEADER_CHANNELS = "x-audio-channels"
_HEADER_BIT_DEPTH = "x-audio-bit-depth"
_HEADER_DURATION = "x-audio-duration-seconds"
_HEADER_EARLIEST = "x-audio-earliest-packet-age-seconds"
_HEADER_LATEST = "x-audio-latest-packet-age-seconds"
_HEADER_LOOKBACK = "x-audio-lookback-seconds"
_HEADER_CONTENT_DISPOSITION = "content-disposition"


@dataclass(frozen=True)
class TimeshiftDownload:
    """Container for downloaded PCM data and associated metadata."""

    pcm_bytes: bytes
    sample_rate: int
    channels: int
    bit_depth: int
    duration_seconds: float
    earliest_packet_age_seconds: float
    latest_packet_age_seconds: float
    lookback_seconds: float
    suggested_filename: str

    def write_to(self, path: Path) -> Path:
        """
        Persist the PCM payload to ``path`` and return the resolved location.

        Parameters
        ----------
        path:
            Destination file.  Parent directories are created automatically.
        """
        path = path.expanduser().resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(self.pcm_bytes)
        return path


class TimeshiftClient:
    """
    Convenience wrapper around the timeshift export REST endpoint.

    Parameters
    ----------
    base_url:
        Root URL of the ScreamRouter API (e.g. ``https://router.local``).
    timeout:
        Request timeout in seconds.
    client:
        Optional pre-configured :class:`httpx.Client` instance for advanced usage.
    """

    def __init__(
        self,
        base_url: str,
        *,
        timeout: float = 60.0,
        client: Optional[httpx.Client] = None,
    ) -> None:
        self._base_url = base_url.rstrip("/")
        self._timeout = timeout
        self._client = client

    def download(
        self,
        source_tag: str,
        *,
        lookback_seconds: float = 300.0,
        stream: bool = False,
    ) -> TimeshiftDownload:
        """
        Retrieve PCM bytes for ``source_tag``.

        Parameters
        ----------
        source_tag:
            The concrete source identifier (e.g. RTP tag or host IP) exactly as
            returned by the stats endpoints.
        lookback_seconds:
            How much history to include (capped by the server-side buffer).
        stream:
            When ``True`` the download is streamed incrementally; leave ``False``
            for in-memory usage.
        """
        if not source_tag:
            raise ValueError("source_tag must be a non-empty string")

        params = {"lookback_seconds": lookback_seconds}
        request_url = f"{self._base_url}/api/timeshift/{quote(source_tag, safe='')}"

        if self._client is not None:
            response = self._client.get(
                request_url,
                params=params,
                timeout=self._timeout,
                follow_redirects=True,
            )
        else:
            with httpx.Client(timeout=self._timeout, follow_redirects=True) as client:
                response = client.get(
                    request_url,
                    params=params,
                    stream=stream,
                )

        response.raise_for_status()
        payload = response.content
        metadata = _TimeshiftMetadata.from_headers(response.headers, lookback_seconds)

        return TimeshiftDownload(
            pcm_bytes=payload,
            sample_rate=metadata.sample_rate,
            channels=metadata.channels,
            bit_depth=metadata.bit_depth,
            duration_seconds=metadata.duration_seconds,
            earliest_packet_age_seconds=metadata.earliest_packet_age_seconds,
            latest_packet_age_seconds=metadata.latest_packet_age_seconds,
            lookback_seconds=metadata.lookback_seconds,
            suggested_filename=metadata.filename,
        )


@dataclass(frozen=True)
class _TimeshiftMetadata:
    sample_rate: int
    channels: int
    bit_depth: int
    duration_seconds: float
    earliest_packet_age_seconds: float
    latest_packet_age_seconds: float
    lookback_seconds: float
    filename: str

    @classmethod
    def from_headers(cls, headers: httpx.Headers, fallback_lookback: float) -> "_TimeshiftMetadata":
        def _parse_int(name: str) -> int:
            raw = headers.get(name)
            if raw is None:
                return 0
            try:
                return int(float(raw))
            except (TypeError, ValueError):
                return 0

        def _parse_float(name: str, default: float = 0.0) -> float:
            raw = headers.get(name)
            if raw is None:
                return default
            try:
                return float(raw)
            except (TypeError, ValueError):
                return default

        filename = _extract_filename(headers.get(_HEADER_CONTENT_DISPOSITION))

        return cls(
            sample_rate=_parse_int(_HEADER_SAMPLE_RATE),
            channels=_parse_int(_HEADER_CHANNELS),
            bit_depth=_parse_int(_HEADER_BIT_DEPTH),
            duration_seconds=_parse_float(_HEADER_DURATION),
            earliest_packet_age_seconds=_parse_float(_HEADER_EARLIEST),
            latest_packet_age_seconds=_parse_float(_HEADER_LATEST),
            lookback_seconds=_parse_float(_HEADER_LOOKBACK, fallback_lookback),
            filename=filename,
        )


def _extract_filename(disposition: Optional[str]) -> str:
    if not disposition:
        return "timeshift.pcm"
    parts = (segment.strip() for segment in disposition.split(";"))
    for part in parts:
        if part.lower().startswith("filename="):
            _, _, value = part.partition("=")
            return value.strip('"') or "timeshift.pcm"
    return "timeshift.pcm"


__all__ = ["TimeshiftClient", "TimeshiftDownload"]

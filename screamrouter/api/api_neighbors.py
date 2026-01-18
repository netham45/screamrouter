"""Proxy APIs for interacting with neighboring ScreamRouter instances."""
from __future__ import annotations

import json
from typing import Any, Dict

import httpx
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field, field_validator

from screamrouter.screamrouter_logger.screamrouter_logger import get_logger

logger = get_logger(__name__)


class NeighborInstance(BaseModel):
    """Description of a neighboring ScreamRouter instance."""

    hostname: str = Field(description="Hostname or DNS name advertised by the neighbor")
    address: str | None = Field(
        default=None,
        description="Direct IP address if available (preferred when DNS is not resolvable)",
    )
    port: int = Field(default=443, ge=1, le=65535, description="API port exposed by the neighbor")
    scheme: str = Field(default="https", description="http or https")
    api_path: str = Field(default="/", description="Base path prefix for the neighbor API")
    verify_tls: bool = Field(default=False, description="Whether to verify TLS certificates when using HTTPS")
    timeout: float = Field(default=5.0, ge=0.5, le=30.0, description="Request timeout when contacting the neighbor")

    @field_validator("scheme")
    @classmethod
    def _normalize_scheme(cls, value: str) -> str:
        normalized = (value or "https").lower()
        if normalized not in {"http", "https"}:
            msg = f"Unsupported scheme '{value}'"
            raise ValueError(msg)
        return normalized

    @field_validator("api_path")
    @classmethod
    def _normalize_api_path(cls, value: str) -> str:
        sanitized = (value or "/").strip() or "/"
        if not sanitized.startswith("/"):
            sanitized = f"/{sanitized}"
        return sanitized

    def build_base_url(self) -> str:
        """Return the neighbor base URL without a trailing slash."""
        default_port = 443 if self.scheme == "https" else 80
        target_host = (self.address or self.hostname or "").strip()
        if not target_host:
            msg = "Neighbor host/address not provided"
            raise ValueError(msg)
        host_port = target_host
        if self.port != default_port:
            host_port = f"{host_port}:{self.port}"

        base = f"{self.scheme}://{host_port}"
        return base


class APINeighbors:
    """Expose helper APIs for interacting with neighboring routers."""

    def __init__(self, app: FastAPI):
        self._app = app
        self._app.add_api_route(
            "/neighbors/sinks",
            self.get_neighbor_sinks,
            methods=["POST"],
            tags=["Neighbors"],
        )

    async def get_neighbor_sinks(self, neighbor: NeighborInstance) -> Any:
        """Fetch /sinks from another ScreamRouter instance."""
        base_url = neighbor.build_base_url()
        sinks_url = f"{base_url}/sinks"
        logger.info("Fetching neighbor sinks from %s", sinks_url)
        try:
            async with httpx.AsyncClient(timeout=neighbor.timeout, verify=neighbor.verify_tls) as client:
                response = await client.get(sinks_url)
                response.raise_for_status()
        except httpx.HTTPStatusError as exc:
            logger.warning(
                "Neighbor responded with HTTP %s for %s",
                exc.response.status_code,
                sinks_url,
            )
            raise HTTPException(
                status_code=502,
                detail=f"Neighbor responded with HTTP {exc.response.status_code}",
            ) from exc
        except httpx.RequestError as exc:
            logger.warning("Neighbor request to %s failed: %s", sinks_url, exc)
            raise HTTPException(
                status_code=502, detail="Unable to reach neighbor sinks endpoint"
            ) from exc

        try:
            payload = response.json()
        except json.JSONDecodeError as exc:
            logger.warning("Neighbor %s returned invalid JSON", sinks_url)
            raise HTTPException(
                status_code=502, detail="Neighbor response was not valid JSON"
            ) from exc

        return payload

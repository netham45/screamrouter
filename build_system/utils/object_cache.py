"""Object file caching utilities for the ScreamRouter build."""

from __future__ import annotations

import hashlib
import os
import shutil
from pathlib import Path
from typing import Iterable, Optional, Sequence


class ObjectCache:
    """Content-addressable cache for compiled object files."""

    def __init__(self, cache_root: Path, enabled: Optional[bool] = None):
        """Initialise the cache container.

        Args:
            cache_root: Base directory for cache artefacts; the cache will
                manage an ``objects`` subdirectory beneath this path.
            enabled: Optional override to force-enable/disable the cache. If
                ``None`` the ``SCREAMROUTER_DISABLE_OBJECT_CACHE`` environment
                variable controls behaviour (``1``/``true`` disable the cache).
        """
        self.base_dir = Path(cache_root)
        self.cache_dir = self.base_dir / "objects"

        if enabled is None:
            env_value = os.environ.get("SCREAMROUTER_DISABLE_OBJECT_CACHE", "").lower()
            enabled = env_value not in {"1", "true", "yes", "on"}
        self._enabled = bool(enabled)

        if self._enabled:
            self.cache_dir.mkdir(parents=True, exist_ok=True)

    @property
    def enabled(self) -> bool:
        """Return whether the cache is active."""
        return self._enabled

    def fingerprint(
        self,
        source: Path,
        compiler: Sequence[str],
        compile_args: Iterable[str],
        extra_key: Optional[Sequence[str]] = None,
    ) -> Optional[str]:
        """Create a deterministic fingerprint for the given translation unit.

        Returns ``None`` when caching is disabled.
        """
        if not self.enabled:
            return None

        hasher = hashlib.sha256()
        hasher.update(str(source.resolve()).encode("utf-8"))

        # Include the source contents so edits invalidate cached artefacts.
        try:
            hasher.update(source.read_bytes())
        except FileNotFoundError:
            return None

        # Canonicalise compiler invocation and flags.
        hasher.update("\0".join(map(str, compiler)).encode("utf-8"))
        normalised_args = sorted(str(arg) for arg in compile_args)
        hasher.update("\0".join(normalised_args).encode("utf-8"))

        if extra_key:
            hasher.update("\0".join(map(str, extra_key)).encode("utf-8"))

        return hasher.hexdigest()

    def _object_path(self, fingerprint: str) -> Path:
        bucket = fingerprint[:2]
        return self.cache_dir / bucket / f"{fingerprint}.o"

    def get_cached_object(self, fingerprint: Optional[str]) -> Optional[Path]:
        """Return the cached object path if it exists and caching is enabled."""
        if not self.enabled or not fingerprint:
            return None

        obj_path = self._object_path(fingerprint)
        if obj_path.exists():
            return obj_path
        return None

    def store_object(self, fingerprint: Optional[str], artifact: Path) -> Optional[Path]:
        """Persist a freshly compiled object under the cache directory."""
        if not self.enabled or not fingerprint:
            return None

        destination = self._object_path(fingerprint)
        destination.parent.mkdir(parents=True, exist_ok=True)

        try:
            shutil.copy2(artifact, destination)
        except (FileNotFoundError, shutil.Error, OSError):
            return None

        return destination

    def clear(self) -> None:
        """Remove all cached objects."""
        if not self.cache_dir.exists():
            return
        shutil.rmtree(self.cache_dir, ignore_errors=True)
        self.cache_dir.mkdir(parents=True, exist_ok=True)


__all__ = ["ObjectCache"]

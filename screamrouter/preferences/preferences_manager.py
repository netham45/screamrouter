"""Thread-safe preferences persistence manager"""

from __future__ import annotations

import json
import os
import threading
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Mapping

from pydantic import ValidationError

import screamrouter.constants.constants as constants
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.preferences import Preferences


class PreferencesManagerError(RuntimeError):
    """Base exception for preferences manager errors"""


class PreferencesStorageError(PreferencesManagerError):
    """Raised when the preferences file cannot be accessed"""


class PreferencesCorruptedError(PreferencesManagerError):
    """Raised when stored preferences data is invalid"""


class PreferencesManager:
    """Handles reading and writing preferences to disk safely"""

    _WINDOWS_LOCK_RANGE = 0x7FFFFFFF

    def __init__(self, preferences_path: str | os.PathLike[str] | None = None):
        self._logger = get_logger(__name__)
        self._preferences_path = Path(preferences_path or constants.PREFERENCES_PATH)
        self._lock = threading.RLock()
        self._ensure_file()

    def get_preferences(self) -> Preferences:
        """Return the persisted preferences, resetting to defaults if invalid"""
        try:
            raw_preferences = self._read_raw_preferences()
        except PreferencesCorruptedError as exc:
            self._logger.warning("Preferences file corrupted; reinitializing defaults: %s", exc)
            return self._reset_to_defaults()
        except PreferencesStorageError:
            raise

        try:
            return Preferences.model_validate(raw_preferences)
        except ValidationError as exc:
            self._logger.warning("Preferences validation failed; reinitializing defaults: %s", exc)
            return self._reset_to_defaults()

    def update_preferences(self, partial_update: Any) -> Preferences:
        """Merge a partial update into persisted preferences"""
        update_payload = self._normalise_update_payload(partial_update)
        if not update_payload:
            raise ValueError("Partial update must contain at least one field")

        with self._open_locked("r+", exclusive=True) as file_obj:
            try:
                file_obj.seek(0)
                current_data = json.load(file_obj)
                if not isinstance(current_data, dict):
                    raise ValueError("Stored preferences root is not an object")
            except (json.JSONDecodeError, ValueError) as exc:
                self._logger.warning("Preferences file invalid during update; resetting before apply: %s", exc)
                current_data = self._default_preferences().model_dump(mode="json")
            except OSError as exc:
                raise PreferencesStorageError("Unable to read preferences for update") from exc

            merged = self._deep_merge_dict(current_data, update_payload)

            try:
                preferences = Preferences.model_validate(merged)
            except ValidationError as exc:
                self._logger.error("Rejected invalid preferences update: %s", exc)
                raise ValueError("Invalid preferences update payload") from exc

            serialized = preferences.model_dump(mode="json")

            try:
                file_obj.seek(0)
                json.dump(serialized, file_obj, indent=2, sort_keys=True)
                file_obj.truncate()
                file_obj.flush()
                os.fsync(file_obj.fileno())
            except OSError as exc:
                raise PreferencesStorageError("Failed to write preferences to disk") from exc

        return preferences

    def _normalise_update_payload(self, partial_update: Any) -> dict[str, Any]:
        if partial_update is None:
            raise ValueError("Partial update cannot be None")

        if hasattr(partial_update, "model_dump"):
            data = partial_update.model_dump(exclude_unset=True)  # type: ignore[attr-defined]
        elif isinstance(partial_update, Mapping):
            data = dict(partial_update)
        else:
            raise TypeError("Partial update must be a mapping or pydantic model")

        # Recursively convert any pydantic models nested inside the payload
        def _convert(value: Any) -> Any:
            if hasattr(value, "model_dump"):
                return value.model_dump(exclude_unset=True)  # type: ignore[attr-defined]
            if isinstance(value, Mapping):
                return {key: _convert(val) for key, val in value.items()}
            return value

        return {key: _convert(val) for key, val in data.items()}

    def _read_raw_preferences(self) -> dict[str, Any]:
        with self._open_locked("r", exclusive=False) as file_obj:
            try:
                raw_data = json.load(file_obj)
            except json.JSONDecodeError as exc:
                raise PreferencesCorruptedError("Preferences file contains invalid JSON") from exc
            except OSError as exc:
                raise PreferencesStorageError("Unable to read preferences file") from exc

        if not isinstance(raw_data, dict):
            raise PreferencesCorruptedError("Preferences data must be a JSON object")

        return raw_data

    def _reset_to_defaults(self) -> Preferences:
        default_preferences = self._default_preferences()
        self._write_preferences(default_preferences)
        return default_preferences

    def _write_preferences(self, preferences: Preferences) -> None:
        serialized = preferences.model_dump(mode="json")
        with self._open_locked("r+", exclusive=True) as file_obj:
            try:
                file_obj.seek(0)
                json.dump(serialized, file_obj, indent=2, sort_keys=True)
                file_obj.truncate()
                file_obj.flush()
                os.fsync(file_obj.fileno())
            except OSError as exc:
                raise PreferencesStorageError("Unable to write preferences") from exc

    def _default_preferences(self) -> Preferences:
        return Preferences()

    def _ensure_file(self) -> None:
        with self._lock:
            try:
                self._preferences_path.parent.mkdir(parents=True, exist_ok=True)
            except OSError as exc:
                raise PreferencesStorageError("Unable to create preferences directory") from exc

            if not self._preferences_path.exists():
                try:
                    with open(self._preferences_path, "w", encoding="utf-8") as file_obj:
                        json.dump(self._default_preferences().model_dump(mode="json"),
                                  file_obj, indent=2, sort_keys=True)
                except OSError as exc:
                    raise PreferencesStorageError("Unable to create preferences file") from exc

    def _deep_merge_dict(self, original: Mapping[str, Any], updates: Mapping[str, Any]) -> dict[str, Any]:
        merged: dict[str, Any] = dict(original)
        for key, value in updates.items():
            if isinstance(value, Mapping) and isinstance(merged.get(key), Mapping):
                merged[key] = self._deep_merge_dict(merged[key], value)  # type: ignore[arg-type]
            else:
                merged[key] = value
        return merged

    @contextmanager
    def _open_locked(self, mode: str, *, exclusive: bool):
        with self._lock:
            self._ensure_file()
            try:
                file_obj = open(self._preferences_path, mode, encoding="utf-8")
            except OSError as exc:
                raise PreferencesStorageError("Unable to open preferences file") from exc

            try:
                self._acquire_file_lock(file_obj, exclusive=exclusive)
            except Exception:
                file_obj.close()
                raise

            try:
                yield file_obj
            finally:
                try:
                    self._release_file_lock(file_obj)
                finally:
                    file_obj.close()

    def _acquire_file_lock(self, file_obj, *, exclusive: bool) -> None:
        if os.name == "posix":  # Linux, macOS, etc.
            import fcntl  # type: ignore

            lock_type = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
            fcntl.flock(file_obj.fileno(), lock_type)
        else:  # Windows
            import msvcrt  # type: ignore

            mode = msvcrt.LK_LOCK if exclusive else msvcrt.LK_RLCK
            file_obj.seek(0)
            msvcrt.locking(file_obj.fileno(), mode, self._WINDOWS_LOCK_RANGE)
            file_obj.seek(0)

    def _release_file_lock(self, file_obj) -> None:
        if os.name == "posix":
            import fcntl  # type: ignore

            fcntl.flock(file_obj.fileno(), fcntl.LOCK_UN)
        else:
            import msvcrt  # type: ignore

            file_obj.seek(0)
            msvcrt.locking(file_obj.fileno(), msvcrt.LK_UNLCK, self._WINDOWS_LOCK_RANGE)
            file_obj.seek(0)


"""API endpoints for managing user preferences"""

from __future__ import annotations

from typing import Any, Dict

from fastapi import FastAPI, HTTPException

from screamrouter.preferences.preferences_manager import (
    PreferencesManager,
    PreferencesManagerError,
    PreferencesStorageError,
)
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger


logger = get_logger(__name__)


class APIPreferences:
    """Expose preferences management endpoints"""

    def __init__(self, app: FastAPI, preferences_manager: PreferencesManager | None = None):
        self._app = app
        """FastAPI application instance"""
        self._preferences_manager = preferences_manager or PreferencesManager()
        """Persistent preferences manager"""

        self._app.get("/preferences", tags=["Preferences"])(self.get_preferences)
        self._app.put("/preferences", tags=["Preferences"])(self.update_preferences)

    async def get_preferences(self):
        """Return the current preferences"""
        try:
            preferences = self._preferences_manager.get_preferences()
        except PreferencesStorageError as exc:
            logger.error("Failed to read preferences: %s", exc)
            raise HTTPException(status_code=500, detail="Unable to read preferences") from exc

        return preferences

    async def update_preferences(self, payload: Dict[str, Any]):
        """Apply a partial update to the preferences"""
        if not isinstance(payload, dict):
            raise HTTPException(status_code=400, detail="Request body must be a JSON object")

        try:
            updated_preferences = self._preferences_manager.update_preferences(payload)
        except (TypeError, ValueError) as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        except PreferencesStorageError as exc:
            logger.error("Failed to persist preferences: %s", exc)
            raise HTTPException(status_code=500, detail="Unable to save preferences") from exc
        except PreferencesManagerError as exc:
            logger.error("Unexpected preferences error: %s", exc)
            raise HTTPException(status_code=500, detail="Unexpected preferences error") from exc

        return updated_preferences


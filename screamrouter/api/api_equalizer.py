"""API Endpoints for managing Equalizer configurations"""

import yaml
from fastapi import FastAPI, HTTPException

from screamrouter.constants.constants import EQUALIZER_CONFIG_PATH
from screamrouter.screamrouter_types.configuration import Equalizer


class APIEqualizer:
    """Holds the API endpoints for managing Equalizer configurations"""
    def __init__(self, main_api: FastAPI):
        self.main_api = main_api
        """Main FastAPI instance"""
        self.equalizers: dict[str, Equalizer] = {}
        try:
            with open(EQUALIZER_CONFIG_PATH, "r", encoding="UTF-8") as f:
                self.equalizers = yaml.unsafe_load(f) or {}
        except FileNotFoundError:
            self.equalizers = {}

        # Add routes
        self.main_api.post("/equalizers/")(self.save_equalizer)
        self.main_api.get("/equalizers/")(self.list_equalizers)
        self.main_api.delete("/equalizers/{name}")(self.delete_equalizer)

    async def save_equalizer(self, equalizer: Equalizer):
        """Save a new equalizer configuration"""
        if not equalizer.name:
            raise HTTPException(status_code=400, detail="Equalizer must have a name")
        self.equalizers[equalizer.name] = equalizer
        with open(EQUALIZER_CONFIG_PATH, "w", encoding="UTF-8") as f:
            yaml.dump(self.equalizers, f)
        return {"message": "Equalizer saved successfully"}

    async def list_equalizers(self):
        """List all equalizer configurations"""
        return {"equalizers": list(self.equalizers.values())}

    async def delete_equalizer(self, name: str):
        """Delete an equalizer configuration by name"""
        if name not in self.equalizers:
            raise HTTPException(status_code=404, detail="Equalizer not found")
        del self.equalizers[name]
        with open(EQUALIZER_CONFIG_PATH, "w", encoding="UTF-8") as f:
            yaml.dump(self.equalizers, f)
        return {"message": "Equalizer deleted successfully"}

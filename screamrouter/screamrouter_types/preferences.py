"""Contains models for persisted user preferences"""

from __future__ import annotations

from datetime import datetime, timezone

from pydantic import AliasChoices, BaseModel, ConfigDict, Field, model_validator


class TutorialProgress(BaseModel):
    """Tracks the user's progress through the tutorial"""

    model_config = ConfigDict(from_attributes=True,
                              json_schema_serialization_defaults_required=True)

    current_step: int = Field(default=0, ge=0)
    """Identifier for the currently active tutorial step"""
    completed: bool = False
    """True once the tutorial has been fully completed"""
    completed_at: datetime | None = None
    """Timestamp for when the tutorial was completed"""
    completed_steps: list[str] = Field(
        default_factory=list,
        validation_alias=AliasChoices("completed_steps", "completedSteps"),
    )
    """Identifiers of tutorial steps the user has completed"""
    version: int = Field(default=2, ge=1)
    """Version of the tutorial schema the client understands"""

    @model_validator(mode="after")
    def _sync_completion_timestamp(self) -> "TutorialProgress":
        """Ensure completion timestamp aligns with completion flag"""
        if self.completed:
            if self.completed_at is None:
                self.completed_at = datetime.now(timezone.utc)
        else:
            self.completed_at = None
        return self


class Preferences(BaseModel):
    """Root model for all persisted user preferences"""

    model_config = ConfigDict(from_attributes=True,
                              json_schema_serialization_defaults_required=True)

    tutorial: TutorialProgress = Field(default_factory=TutorialProgress)
    """Tutorial progress information"""
    mdns_responder_uuid: str | None = Field(
        default=None,
        validation_alias=AliasChoices("mdns_responder_uuid", "mdnsResponderUuid"),
    )
    """Persisted identifier for the mDNS responder instance"""
    mdns_router_uuid: str | None = Field(
        default=None,
        validation_alias=AliasChoices("mdns_router_uuid", "mdnsRouterUuid"),
    )
    """Persisted identifier for the mDNS router advertiser instance"""


__all__ = [
    "Preferences",
    "TutorialProgress",
]

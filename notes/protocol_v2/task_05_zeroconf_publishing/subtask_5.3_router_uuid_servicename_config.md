# Sub-Task 5.3: Ensure Router UUID and Service Name Configuration

**Objective:** Ensure that `ConfigurationManager` can provide a persistent `router_uuid` for the ScreamRouter instance and a configurable `service_instance_name` for mDNS publishing.

**Parent Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)
**Previous Sub-Task:** [Sub-Task 5.2: Integrate `SipMdnsPublisher` with `SipManager`](./subtask_5.2_integrate_mdns_publisher_with_sipmanager.md)
**Related Task:** [Python Configuration System Updates for Protocol v2](../task_04_python_config_updates.md) (specifically Sub-Task 4.2)

## Key Steps & Considerations:

1.  **Define Global Settings Model (`ScreamRouterSettings`):**
    *   **File:** `src/screamrouter_types/configuration.py`
    *   If a Pydantic model for global application settings doesn't exist, create one. This model will hold the `router_uuid` and `zeroconf_service_name`.
    ```python
    # In src/screamrouter_types/configuration.py
    from pydantic import BaseModel, Field
    from typing import Optional
    import uuid as uuid_generator # To avoid conflict with a field named 'uuid'

    class ScreamRouterSettings(BaseModel):
        router_uuid: Optional[str] = Field(default=None, description="Unique identifier for this ScreamRouter instance.")
        # User-friendly part of the mDNS service name, e.g., "Living Room" -> "Living Room._screamrouter-sip._udp.local."
        zeroconf_instance_name: str = Field(default="ScreamRouter", description="User-friendly instance name for mDNS service.") 
        # Add other global settings here as needed, e.g., default SIP port, domain
        default_sip_port: int = Field(default=5060, description="Default SIP listening port.")
        default_sip_domain: str = Field(default="screamrouter.local", description="Default SIP domain for this router.")

        # model_config = { ... } # For Pydantic v2, if using aliases etc.
    ```

2.  **Integrate `ScreamRouterSettings` into Main Configuration:**
    *   **File:** `src/screamrouter_types/configuration.py`
    *   The main `ScreamRouterConfig` Pydantic model (which holds lists of sources, sinks, routes, etc.) should include an instance of `ScreamRouterSettings`.
    ```python
    # class ScreamRouterConfig(BaseModel):
    #     settings: ScreamRouterSettings = Field(default_factory=ScreamRouterSettings)
    #     sources: List[SourceDescription] = Field(default_factory=list)
    #     sinks: List[SinkDescription] = Field(default_factory=list)
    #     # ... other fields ...
    ```

3.  **Implement Accessor Methods in `ConfigurationManager`:**
    *   **File:** `src/configuration/configuration_manager.py`
    *   **`get_router_uuid(self) -> str`:**
        *   Access `self.screamrouter_config.settings.router_uuid`.
        *   If it's `None` or empty, generate a new UUID using `str(uuid_generator.uuid4())`.
        *   Store the new UUID back into `self.screamrouter_config.settings.router_uuid`.
        *   Trigger a save of the configuration (`self.__save_config()`) to persist the new UUID.
        *   Return the UUID.
    ```python
    # In ConfigurationManager
    # import uuid as uuid_generator

    # def get_router_uuid(self) -> str:
    #     if not self.screamrouter_config.settings.router_uuid:
    #         new_uuid = str(uuid_generator.uuid4())
    #         logger.info(f"Generated new router UUID: {new_uuid}")
    #         self.screamrouter_config.settings.router_uuid = new_uuid
    #         self.__save_config() # Persist the new UUID
    #     return self.screamrouter_config.settings.router_uuid
    ```
    *   **`get_zeroconf_instance_name(self) -> str`:**
        *   Return `self.screamrouter_config.settings.zeroconf_instance_name`. This will use the Pydantic default ("ScreamRouter") if not set in `config.yaml`.
    ```python
    # In ConfigurationManager
    # def get_zeroconf_instance_name(self) -> str:
    #     return self.screamrouter_config.settings.zeroconf_instance_name
    ```
    *   **`get_sip_port(self) -> int` and `get_sip_domain(self) -> str`:**
        *   Similar accessors for SIP port and domain from `ScreamRouterSettings` for `SipManager` initialization.

4.  **Update `ConfigurationManager.__load_config()` and `__init__()`:**
    *   Ensure that `self.screamrouter_config.settings` is properly initialized when a new config is created or an old one is loaded (Pydantic's `default_factory` for `ScreamRouterSettings` in `ScreamRouterConfig` should handle this).
    *   When loading, if the `settings` block or specific fields like `router_uuid` are missing, Pydantic will use the defaults. The `get_router_uuid()` logic will then ensure one is generated and saved on first access if needed.

## Code Alterations:

*   **`src/screamrouter_types/configuration.py`:**
    *   Define or update `ScreamRouterSettings` Pydantic model.
    *   Ensure `ScreamRouterConfig` includes a `settings: ScreamRouterSettings` field.
*   **`src/configuration/configuration_manager.py`:**
    *   Implement `get_router_uuid()`, ensuring UUID generation and persistence if not present.
    *   Implement `get_zeroconf_instance_name()`.
    *   Implement `get_sip_port()` and `get_sip_domain()`.
    *   Ensure `SipManager` is initialized using these values from `ConfigurationManager`.

## Recommendations:

*   **Persistence of UUID:** It's crucial that the `router_uuid` is generated once and then persists across restarts. Saving the configuration after generating a new UUID ensures this.
*   **User Configuration for Service Name:** While a default `zeroconf_instance_name` is good, this should ideally be configurable by the user (e.g., via an advanced settings section in the UI or directly in `config.yaml`).
*   **Default SIP Port/Domain:** Storing default SIP port and domain in `ScreamRouterSettings` makes them configurable and accessible to `SipManager`.

## Acceptance Criteria:

*   `ScreamRouterSettings` Pydantic model is defined with fields for `router_uuid`, `zeroconf_instance_name`, `default_sip_port`, and `default_sip_domain`.
*   `ConfigurationManager` can provide a persistent `router_uuid`, generating and saving one if it doesn't exist.
*   `ConfigurationManager` can provide the `zeroconf_instance_name`, `default_sip_port`, and `default_sip_domain` from settings.
*   These settings are correctly loaded from and saved to `config.yaml`.
*   `SipManager` is initialized using these configured values.

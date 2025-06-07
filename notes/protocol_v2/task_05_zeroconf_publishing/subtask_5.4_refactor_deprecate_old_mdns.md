# Sub-Task 5.4: Refactor/Deprecate Old mDNS Utilities

**Objective:** Review existing mDNS utilities (`src/utils/mdns_responder.py`, `mdns_pinger.py`, `mdns_settings_pinger.py`) and remove any logic related to advertising a generic ScreamRouter service if it's superseded by the new SIP SRV record publication. Clearly document the new discovery mechanism.

**Parent Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)
**Previous Sub-Task:** [Sub-Task 5.3: Ensure Router UUID and Service Name Configuration](./subtask_5.3_router_uuid_servicename_config.md)

## Key Steps & Considerations:

1.  **Analyze Existing mDNS Utilities:**
    *   **`src/utils/mdns_responder.py`:**
        *   **Current Functionality:** Likely advertises a generic ScreamRouter service (e.g., `_screamrouter._udp.local.`) and responds to queries for it. This might provide basic information like the router's IP and a port for the legacy protocol or web UI.
        *   **Protocol v2 Impact:** For Protocol v2 clients, the primary discovery mechanism will be the SIP SRV record (`_screamrouter-sip._udp.local.` or `_sip._udp.local.`).
        *   **Decision:**
            *   If the generic service advertisement is *only* for legacy clients or for discovering the web UI (and not SIP), it might be kept but clearly distinguished.
            *   If its purpose overlaps with SIP discovery or is now redundant, the service advertisement part should be removed or disabled by default for Protocol v2 setups.
            *   The "responder" part (answering queries) might still be relevant if it provides information not covered by the SIP SRV record's TXT fields.
    *   **`src/utils/mdns_pinger.py` & `mdns_settings_pinger.py`:**
        *   **Current Functionality:** These likely *browse* for other Scream devices or services on the network, or ping specific devices to check their status or sync settings using mDNS.
        *   **Protocol v2 Impact:** Device discovery and presence for Protocol v2 devices will primarily be handled by SIP (clients REGISTERing with ScreamRouter).
        *   **Decision:**
            *   If these utilities are for discovering *legacy* Scream devices that *don't* support SIP, they might still be needed for backward compatibility.
            *   If they are for discovering devices that *will* be SIP-enabled, their functionality is largely superseded by SIP presence.
            *   Consider if any "pinging" or "settings sync" mechanism here is still valid or if that too will be handled via SIP (e.g., SIP OPTIONS for keep-alives, specific SIP messages for settings).

2.  **Refactoring/Deprecation Strategy:**
    *   **Priority for SIP Discovery:** The new `SipMdnsPublisher` advertising the SIP SRV record is the primary method for Protocol v2.
    *   **Legacy Support:**
        *   If `mdns_responder.py`'s generic service is still needed for legacy clients or non-SIP purposes (like web UI discovery if it's on a different port), ensure its service type and information are distinct from the SIP service.
        *   It might be useful to make the old mDNS responder's activity configurable (on/off via `config.yaml`).
    *   **Client-Side Discovery (`mdns_pinger.py`):**
        *   If ScreamRouter still needs to *discover* legacy clients that don't use SIP/Zeroconf for registration, this pinger might remain.
        *   However, the Protocol v2 design emphasizes clients discovering and registering with the ScreamRouter SIP server. ScreamRouter acting as an mDNS *browser* for other ScreamRouters or specific types of clients might be less relevant unless it's for a specific inter-router communication feature not covered by SIP federation (which is not in the current scope).

3.  **Code Changes:**
    *   **`src/utils/mdns_responder.py`:**
        *   If keeping parts of it: Clearly comment its purpose in the context of Protocol v2.
        *   If removing SIP-like advertisement: Delete or comment out the `ServiceInfo` registration for the old generic service if it's now fully replaced by the SIP SRV record.
        *   Ensure it doesn't conflict with `SipMdnsPublisher` (e.g., by trying to use the same `Zeroconf` instance in conflicting ways if not managed carefully, or advertising conflicting information).
    *   **`src/utils/mdns_pinger.py`, `src/utils/mdns_settings_pinger.py`:**
        *   Evaluate if their functionality (browsing/pinging for other Scream devices) is still required.
        *   If yes, ensure they target legacy devices or fulfill a role not covered by SIP.
        *   If no, mark them for deprecation or remove them. Add comments explaining why they are deprecated.
    *   **`ConfigurationManager` or main application logic:**
        *   Update any code that initializes or uses these old mDNS utilities. If they are disabled or removed, ensure they are no longer started.

4.  **Documentation Updates:**
    *   Clearly document in `protocol_spec_v2.md` and user/developer guides that `_screamrouter-sip._udp.local.` (or the chosen SIP SRV type) is the **primary discovery mechanism** for Protocol v2 clients.
    *   If any legacy mDNS services are kept, document their specific purpose and who they are for.

## Recommendations:

*   **Favor Simplicity:** If functionality is fully covered by the new SIP/Zeroconf mechanism, prefer to remove or clearly deprecate old code to reduce complexity and potential for conflicts.
*   **Configuration Option:** For any retained legacy mDNS functionality, consider adding a boolean flag in `ScreamRouterSettings` to enable/disable it, defaulting to disabled if Protocol v2 is the primary mode.
*   **Testing:** After refactoring, test:
    *   That the new SIP SRV record is published correctly.
    *   That any *retained* legacy mDNS services still function as intended and don't interfere.
    *   That *removed/disabled* legacy mDNS services are indeed no longer advertised.

## Acceptance Criteria:

*   Existing mDNS utilities (`mdns_responder.py`, etc.) have been reviewed.
*   Any mDNS service advertisement logic that is superseded by the new SIP SRV record publication is removed or disabled.
*   Functionality in `mdns_pinger.py` or `mdns_settings_pinger.py` that is replaced by SIP presence is removed or clearly marked for deprecation for Protocol v2 devices.
*   Documentation is updated to reflect the new primary discovery mechanism (SIP SRV record) and the status/purpose of any remaining legacy mDNS utilities.
*   The system operates correctly with the refactored mDNS setup, prioritizing SIP discovery.

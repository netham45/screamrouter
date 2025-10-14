"""Manages temporary sinks and routes for WebRTC listening."""

import threading
import uuid
from typing import Dict, List, Optional, Set, Tuple
from ipaddress import IPv4Address

import screamrouter.screamrouter_logger.screamrouter_logger as screamrouter_logger
from screamrouter.screamrouter_types.configuration import (
    SinkDescription,
    RouteDescription,
    SourceDescription,
    Equalizer
)
from screamrouter.screamrouter_types.annotations import (
    SinkNameType,
    RouteNameType,
    SourceNameType,
    IPAddressType
)

_logger = screamrouter_logger.get_logger(__name__)


class TemporaryEntityManager:
    """
    Manages temporary sinks and routes for WebRTC listening.
    
    This class handles:
    - Creation of temporary sinks for WebRTC listeners
    - Creation of temporary routes from sources to temporary sinks
    - Tracking of entity-to-listener relationships
    - Cleanup of temporary entities when listeners disconnect
    """
    
    def __init__(self):
        """Initialize the TemporaryEntityManager."""
        # Track temporary entities by listener ID
        self._listener_to_sinks: Dict[str, Set[str]] = {}  # listener_id -> set of sink config_ids
        self._listener_to_routes: Dict[str, Set[str]] = {}  # listener_id -> set of route config_ids
        
        # Track sink and route objects
        self._temporary_sinks: Dict[str, SinkDescription] = {}  # config_id -> SinkDescription
        self._temporary_routes: Dict[str, RouteDescription] = {}  # config_id -> RouteDescription
        
        # Track source-to-listeners mapping for efficient lookup
        self._source_to_listeners: Dict[str, Set[str]] = {}  # source_name -> set of listener_ids
        
        # Thread safety
        self._lock = threading.RLock()
        
        _logger.info("[TemporaryEntityManager] Initialized")
    
    def create_temporary_sink_for_source(
        self,
        source: SourceDescription,
        listener_id: str,
        listener_ip: Optional[IPAddressType] = None,
        port: int = 5000,
        protocol: str = "web_receiver"
    ) -> SinkDescription:
        """
        Create a temporary sink for a source to stream to a WebRTC listener.
        
        Args:
            source: The source description to create a sink for
            listener_id: Unique identifier for the WebRTC listener
            listener_ip: Optional IP address for the sink (for non-WebRTC protocols)
            port: Port number for the sink
            protocol: Protocol to use (default: "web_receiver" for WebRTC)
            
        Returns:
            The created temporary SinkDescription
        """
        with self._lock:
            # Generate unique name for the temporary sink
            sink_name = f"WebRTC_{source.name}_{listener_id[:8]}"
            
            # Create the temporary sink
            temp_sink = SinkDescription(
                name=sink_name,
                ip=listener_ip,
                port=port,
                is_group=False,
                enabled=True,
                volume=1.0,
                bit_depth=16,  # WebRTC typically uses 16-bit
                sample_rate=48000,  # Standard WebRTC sample rate
                channels=2,  # Stereo by default
                channel_layout="stereo",
                delay=0,
                equalizer=Equalizer(),
                timeshift=0,
                speaker_layouts={},
                volume_normalization=False,
                time_sync=False,
                time_sync_delay=0,
                config_id=str(uuid.uuid4()),
                use_tcp=False,
                enable_mp3=False,  # WebRTC uses Opus
                protocol=protocol,
                multi_device_mode=False,
                rtp_receiver_mappings=[],
                is_temporary=True  # Mark as temporary
            )
            
            # Store the temporary sink
            self._temporary_sinks[temp_sink.config_id] = temp_sink
            
            # Track the sink for this listener
            if listener_id not in self._listener_to_sinks:
                self._listener_to_sinks[listener_id] = set()
            self._listener_to_sinks[listener_id].add(temp_sink.config_id)
            
            # Track the listener for this source
            if source.name not in self._source_to_listeners:
                self._source_to_listeners[source.name] = set()
            self._source_to_listeners[source.name].add(listener_id)
            
            _logger.info(
                "[TemporaryEntityManager] Created temporary sink '%s' (ID: %s) for source '%s' and listener '%s'",
                sink_name, temp_sink.config_id, source.name, listener_id
            )
            
            return temp_sink
    
    def create_temporary_route(
        self,
        source: SourceDescription,
        sink: SinkDescription,
        listener_id: str
    ) -> RouteDescription:
        """
        Create a temporary route from a source to a sink.
        
        Args:
            source: The source to route from
            sink: The sink to route to
            listener_id: Unique identifier for the WebRTC listener
            
        Returns:
            The created temporary RouteDescription
        """
        with self._lock:
            # Generate unique name for the temporary route
            route_name = f"WebRTC_Route_{source.name}_to_{sink.name}"
            
            # Create the temporary route
            temp_route = RouteDescription(
                name=route_name,
                sink=sink.name,
                source=source.name,
                enabled=True,
                volume=1.0,
                delay=0,
                equalizer=Equalizer(),
                timeshift=0,
                speaker_layouts={},
                config_id=str(uuid.uuid4()),
                is_temporary=True  # Mark as temporary
            )
            
            # Store the temporary route
            self._temporary_routes[temp_route.config_id] = temp_route
            
            # Track the route for this listener
            if listener_id not in self._listener_to_routes:
                self._listener_to_routes[listener_id] = set()
            self._listener_to_routes[listener_id].add(temp_route.config_id)
            
            _logger.info(
                "[TemporaryEntityManager] Created temporary route '%s' (ID: %s) for listener '%s'",
                route_name, temp_route.config_id, listener_id
            )
            
            return temp_route
    
    def get_temporary_sinks_for_listener(self, listener_id: str) -> List[SinkDescription]:
        """
        Get all temporary sinks associated with a listener.
        
        Args:
            listener_id: The listener identifier
            
        Returns:
            List of SinkDescription objects for the listener
        """
        with self._lock:
            sink_ids = self._listener_to_sinks.get(listener_id, set())
            return [
                self._temporary_sinks[sink_id]
                for sink_id in sink_ids
                if sink_id in self._temporary_sinks
            ]
    
    def get_temporary_routes_for_listener(self, listener_id: str) -> List[RouteDescription]:
        """
        Get all temporary routes associated with a listener.
        
        Args:
            listener_id: The listener identifier
            
        Returns:
            List of RouteDescription objects for the listener
        """
        with self._lock:
            route_ids = self._listener_to_routes.get(listener_id, set())
            return [
                self._temporary_routes[route_id]
                for route_id in route_ids
                if route_id in self._temporary_routes
            ]
    
    def get_listeners_for_source(self, source_name: SourceNameType) -> Set[str]:
        """
        Get all listener IDs that are listening to a specific source.
        
        Args:
            source_name: The name of the source
            
        Returns:
            Set of listener IDs
        """
        with self._lock:
            return self._source_to_listeners.get(source_name, set()).copy()
    
    def cleanup_listener_entities(self, listener_id: str) -> Tuple[List[str], List[str]]:
        """
        Remove all temporary entities associated with a listener.
        
        Args:
            listener_id: The listener identifier to clean up
            
        Returns:
            Tuple of (removed_sink_ids, removed_route_ids)
        """
        with self._lock:
            removed_sink_ids = []
            removed_route_ids = []
            
            # Remove temporary sinks
            sink_ids = self._listener_to_sinks.get(listener_id, set())
            for sink_id in sink_ids:
                if sink_id in self._temporary_sinks:
                    sink = self._temporary_sinks.pop(sink_id)
                    removed_sink_ids.append(sink_id)
                    _logger.info(
                        "[TemporaryEntityManager] Removed temporary sink '%s' (ID: %s) for listener '%s'",
                        sink.name, sink_id, listener_id
                    )
            
            # Remove temporary routes
            route_ids = self._listener_to_routes.get(listener_id, set())
            for route_id in route_ids:
                if route_id in self._temporary_routes:
                    route = self._temporary_routes.pop(route_id)
                    removed_route_ids.append(route_id)
                    _logger.info(
                        "[TemporaryEntityManager] Removed temporary route '%s' (ID: %s) for listener '%s'",
                        route.name, route_id, listener_id
                    )
            
            # Clean up listener tracking
            if listener_id in self._listener_to_sinks:
                del self._listener_to_sinks[listener_id]
            if listener_id in self._listener_to_routes:
                del self._listener_to_routes[listener_id]
            
            # Clean up source-to-listener mapping
            for source_name in list(self._source_to_listeners.keys()):
                listeners = self._source_to_listeners[source_name]
                listeners.discard(listener_id)
                if not listeners:
                    del self._source_to_listeners[source_name]
            
            _logger.info(
                "[TemporaryEntityManager] Cleaned up %d sinks and %d routes for listener '%s'",
                len(removed_sink_ids), len(removed_route_ids), listener_id
            )
            
            return removed_sink_ids, removed_route_ids
    
    def cleanup_all_entities(self) -> None:
        """
        Remove all temporary entities. Used during shutdown or reset.
        """
        with self._lock:
            total_sinks = len(self._temporary_sinks)
            total_routes = len(self._temporary_routes)
            
            self._listener_to_sinks.clear()
            self._listener_to_routes.clear()
            self._temporary_sinks.clear()
            self._temporary_routes.clear()
            self._source_to_listeners.clear()
            
            _logger.info(
                "[TemporaryEntityManager] Cleaned up all temporary entities (%d sinks, %d routes)",
                total_sinks, total_routes
            )
    
    def get_all_temporary_sinks(self) -> List[SinkDescription]:
        """
        Get all temporary sinks currently managed.
        
        Returns:
            List of all temporary SinkDescription objects
        """
        with self._lock:
            return list(self._temporary_sinks.values())
    
    def get_all_temporary_routes(self) -> List[RouteDescription]:
        """
        Get all temporary routes currently managed.
        
        Returns:
            List of all temporary RouteDescription objects
        """
        with self._lock:
            return list(self._temporary_routes.values())
    
    def has_listener(self, listener_id: str) -> bool:
        """
        Check if a listener has any temporary entities.
        
        Args:
            listener_id: The listener identifier
            
        Returns:
            True if the listener has temporary entities, False otherwise
        """
        with self._lock:
            return (
                listener_id in self._listener_to_sinks or
                listener_id in self._listener_to_routes
            )
    
    def get_listener_count(self) -> int:
        """
        Get the total number of active listeners.
        
        Returns:
            Number of active listeners
        """
        with self._lock:
            # Union of listeners with sinks and routes
            all_listeners = set(self._listener_to_sinks.keys()) | set(self._listener_to_routes.keys())
            return len(all_listeners)
    
    def get_statistics(self) -> Dict[str, int]:
        """
        Get statistics about temporary entities.
        
        Returns:
            Dictionary with statistics about temporary entities
        """
        with self._lock:
            return {
                "total_listeners": self.get_listener_count(),
                "total_temporary_sinks": len(self._temporary_sinks),
                "total_temporary_routes": len(self._temporary_routes),
                "total_source_mappings": len(self._source_to_listeners),
            }
    
    def create_temporary_entities_for_listener(
        self,
        source: SourceDescription,
        listener_id: str,
        listener_ip: Optional[IPAddressType] = None,
        port: int = 5000,
        protocol: str = "web_receiver"
    ) -> Tuple[SinkDescription, RouteDescription]:
        """
        Convenience method to create both a temporary sink and route for a listener.
        
        Args:
            source: The source to create entities for
            listener_id: Unique identifier for the WebRTC listener
            listener_ip: Optional IP address for the sink
            port: Port number for the sink
            protocol: Protocol to use
            
        Returns:
            Tuple of (created_sink, created_route)
        """
        # Create temporary sink
        temp_sink = self.create_temporary_sink_for_source(
            source, listener_id, listener_ip, port, protocol
        )
        
        # Create temporary route
        temp_route = self.create_temporary_route(
            source, temp_sink, listener_id
        )
        
        return temp_sink, temp_route
    
    def update_listener_sink_settings(
        self,
        listener_id: str,
        sink_id: str,
        **kwargs
    ) -> bool:
        """
        Update settings for a temporary sink.
        
        Args:
            listener_id: The listener identifier
            sink_id: The sink config_id to update
            **kwargs: Settings to update (volume, equalizer, etc.)
            
        Returns:
            True if updated successfully, False otherwise
        """
        with self._lock:
            if sink_id not in self._temporary_sinks:
                _logger.warning(
                    "[TemporaryEntityManager] Sink ID %s not found for listener %s",
                    sink_id, listener_id
                )
                return False
            
            sink = self._temporary_sinks[sink_id]
            
            # Update allowed fields
            allowed_fields = {
                'volume', 'delay', 'equalizer', 'timeshift',
                'volume_normalization', 'bit_depth', 'sample_rate',
                'channels', 'channel_layout'
            }
            
            for field, value in kwargs.items():
                if field in allowed_fields and hasattr(sink, field):
                    setattr(sink, field, value)
                    _logger.debug(
                        "[TemporaryEntityManager] Updated sink %s field '%s' to '%s'",
                        sink_id, field, value
                    )
            
            return True
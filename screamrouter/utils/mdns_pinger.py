"""mDNS pinger for discovering sources and sinks"""
import logging
import threading
import time
from copy import deepcopy
from datetime import datetime, timezone
from typing import Dict, List, Optional, Set

from zeroconf import (DNSIncoming, DNSOutgoing, DNSQuestion, DNSRecord,
                      RecordUpdateListener, ServiceBrowser, ServiceListener,
                      Zeroconf, current_time_millis)

from screamrouter.screamrouter_types.mdns import DiscoveredDevice

# DNS record types and flags
DNS_TYPE_A = 1  # A record type (IPv4 address)
DNS_FLAGS_QR_QUERY = 0x0000  # Standard query

logger = logging.getLogger(__name__)

class ScreamListener(ServiceListener):
    """Listener for Scream mDNS services"""
    def __init__(self):
        self._lock = threading.RLock()
        self.source_ips: Set[str] = set()
        self.sink_ips: Set[str] = set()
        self.service_info: Dict[str, Dict[str, object]] = {}

    def _normalize_properties(self, properties: Optional[Dict[object, object]]) -> Dict[str, str]:
        """Convert TXT record properties into clean string dictionary."""
        if not properties:
            return {}

        normalized: Dict[str, str] = {}
        for raw_key, raw_value in properties.items():
            key = raw_key.decode('utf-8', errors='ignore') if isinstance(raw_key, (bytes, bytearray)) else str(raw_key)

            if isinstance(raw_value, (bytes, bytearray)):
                value = raw_value.decode('utf-8', errors='ignore')
            elif isinstance(raw_value, (list, tuple)):
                value = ','.join(
                    item.decode('utf-8', errors='ignore') if isinstance(item, (bytes, bytearray)) else str(item)
                    for item in raw_value
                )
            else:
                value = str(raw_value)

            normalized[key] = value

        return normalized

    def _record_device(self, ip: str, name: str, port: Optional[int], properties: Dict[str, str], device_type: str) -> None:
        """Persist information about a discovered device."""
        now = datetime.now(timezone.utc)
        with self._lock:
            entry = self.service_info.get(ip, {})
            existing_type = str(entry.get('type', 'unknown')) if entry else 'unknown'
            effective_type = device_type if device_type and device_type != 'unknown' else existing_type
            role = 'source' if effective_type == 'sender' else ('sink' if effective_type == 'receiver' else 'unknown')
            entry.update({
                'name': name,
                'port': port,
                'properties': properties,
                'type': effective_type or 'unknown',
                'role': role,
                'last_seen': now,
                'discovery_method': 'mdns',
            })
            self.service_info[ip] = entry

            if device_type == 'sender':
                self.source_ips.add(ip)
            elif device_type == 'receiver':
                self.sink_ips.add(ip)

    def touch_device(self, ip: str, device_type: Optional[str] = None) -> None:
        """Update last_seen timestamp for a device if we know it."""
        now = datetime.now(timezone.utc)
        with self._lock:
            entry = self.service_info.setdefault(ip, {
                'name': '',
                'port': None,
                'properties': {},
                'type': device_type or 'unknown',
                'role': 'source' if device_type == 'sender' else ('sink' if device_type == 'receiver' else 'unknown'),
                'discovery_method': 'mdns',
            })
            if device_type and device_type != 'unknown':
                entry['type'] = device_type
                entry['role'] = 'source' if device_type == 'sender' else ('sink' if device_type == 'receiver' else 'unknown')
            entry['last_seen'] = now
            if device_type == 'sender':
                self.source_ips.add(ip)
            elif device_type == 'receiver':
                self.sink_ips.add(ip)

    def get_source_ips(self) -> Set[str]:
        with self._lock:
            return set(self.source_ips)

    def get_sink_ips(self) -> Set[str]:
        with self._lock:
            return set(self.sink_ips)

    def get_service_info(self, ip: str) -> Dict[str, object]:
        with self._lock:
            info = self.service_info.get(ip)
            return deepcopy(info) if info else {}

    def serialize_devices(self) -> List[DiscoveredDevice]:
        with self._lock:
            devices: List[DiscoveredDevice] = []
            for ip, data in self.service_info.items():
                raw_properties = data.get('properties', {}) or {}
                properties = {str(k): v for k, v in raw_properties.items()}
                last_seen = data.get('last_seen')
                if isinstance(last_seen, datetime):
                    last_seen_str = last_seen.isoformat()
                else:
                    last_seen_str = str(last_seen) if last_seen else ""
                device_type = data.get('type')
                role = data.get('role') or (
                    'source' if device_type == 'sender' else ('sink' if device_type == 'receiver' else 'unknown')
                )
                devices.append(
                    DiscoveredDevice(
                        discovery_method=str(data.get('discovery_method') or 'mdns'),
                        role=role,
                        ip=ip,
                        port=data.get('port'),
                        name=str(data.get('name')) if data.get('name') else None,
                        tag=properties.get('tag') or properties.get('identifier'),
                        properties=properties,
                        last_seen=last_seen_str,
                        device_type=str(device_type) if device_type else None,
                    )
                )
            return devices
        
    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle new services"""
        info = zc.get_service_info(type_, name)
        if info and info.addresses:
            ip = '.'.join(str(x) for x in info.addresses[0])

            # Parse TXT records to determine device type
            raw_properties = info.properties if info.properties else {}
            properties = self._normalize_properties(raw_properties)
            mode = properties.get('mode', '')
            device_type = properties.get('type', '')
            classified_type = 'sender' if mode == 'sender' or device_type == 'sender' else (
                'receiver' if mode == 'receiver' or device_type == 'receiver' else 'unknown'
            )

            # Store full service info and classification
            self._record_device(ip, name, info.port, properties, classified_type)

            # Classify based on mode/type in TXT records
            if classified_type == 'sender':
                logger.info(f"Discovered new source (sender): {ip} - {name}")
            elif classified_type == 'receiver':
                logger.info(f"Discovered new sink (receiver): {ip} - {name}")
            else:
                logger.warning(f"Service {name} at {ip} has unclear mode/type. Properties: {properties}")
                    
    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle service updates"""
        self.add_service(zc, type_, name)
        
    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Handle service removals - we don't remove IPs as they might still be valid"""
        pass

class MDNSPinger:
    """Discovers Scream sources and sinks using mDNS"""
    def __init__(self, interval: float = 5.0):
        """Initialize MDNSPinger
        
        Args:
            interval: How often to refresh services in seconds
        """
        self.interval = interval
        self.running = False
        self.thread: Optional[threading.Thread] = None
        self.zeroconf: Optional[Zeroconf] = None
        
        # Create listener for handling responses
        self.listener = ScreamListener()
        
        #logger.debug("Initializing Zeroconf mDNS handler")
        
    def start(self):
        """Start the mDNS discovery"""
        if self.running:
            return

        self.running = True
        self.zeroconf = Zeroconf()

        # Browse for _scream._udp services
        self.browser = ServiceBrowser(self.zeroconf, "_scream._udp.local.", self.listener)

        # Start refresh thread
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()

        logger.info("mDNS discovery started for _scream._udp services")
        
    def stop(self):
        """Stop the mDNS discovery"""
        self.running = False
        if self.thread:
            self.thread.join()
        if hasattr(self, 'browser'):
            self.browser.cancel()
        if self.zeroconf:
            self.zeroconf.close()
            self.zeroconf = None
        logger.info("mDNS discovery stopped")
        
    def get_source_ips(self) -> Set[str]:
        """Get set of discovered source IPs"""
        return self.listener.get_source_ips()
        
    def get_sink_ips(self) -> Set[str]:
        """Get set of discovered sink IPs"""
        return self.listener.get_sink_ips()

    def get_service_info(self, ip: str) -> dict:
        """Get full service info for an IP address"""
        return self.listener.get_service_info(ip)

    def serialize_devices(self) -> List[DiscoveredDevice]:
        """Return discovered devices as serializable models."""
        return self.listener.serialize_devices()
    
    def update_record(self, zeroconf: Zeroconf, now: float, record: DNSRecord) -> None:
        """Handle record updates from Zeroconf"""
        #logger.info(f"Received record update: {record}")
        
        if record.type == DNS_TYPE_A:  # A record
            name = record.name.lower()
            #logger.info(f"Processing A record for {name}")
            
            if hasattr(record, 'address'):
                if isinstance(record.address, bytes):
                    ip = '.'.join(str(x) for x in record.address)
                else:
                    ip = record.address
                
                #logger.info(f"A record IP: {ip}")
                
                # Add the IP to the appropriate set regardless of the name
                # This ensures we capture all responses
                if "_source" in name:
                    self.listener.touch_device(ip, 'sender')
                elif "_sink" in name:
                    self.listener.touch_device(ip, 'receiver')
                else:
                    pass
                    #logger.info(f"Unrecognized service name: {name}")
        
    def _send_query(self, hostname: str) -> None:
        """Send an mDNS query and handle responses"""
        if not self.zeroconf:
            return
            
        # Create and send query
        out = DNSOutgoing(DNS_FLAGS_QR_QUERY)
        out.add_question(DNSQuestion(hostname, DNS_TYPE_A, 1))
        self.zeroconf.send(out)
        
        # Wait a bit for responses to arrive
        time.sleep(0.1)
        
        # Register a handler for incoming packets
        class ResponseHandler(RecordUpdateListener):
            def __init__(self, pinger):
                self.pinger = pinger
                
            def update_record(self, zeroconf, now, record):
                if record.type == DNS_TYPE_A:  # A record
                    name = record.name.lower()
                    #logger.info(f"Found A record for {name}")
                    
                    if hasattr(record, 'address'):
                        if isinstance(record.address, bytes):
                            ip = '.'.join(str(x) for x in record.address)
                        else:
                            ip = record.address
                        
                        #logger.info(f"A record IP: {ip}")
                        
                        # For A records from queries, we can't determine type without TXT records
                        # ServiceBrowser will handle proper classification
                        if "_scream" in name:
                            logger.debug(f"Found A record for _scream service at {ip}, waiting for full service info")
                            self.pinger.listener.touch_device(ip)
        
        # Create and register the handler
        handler = ResponseHandler(self)
        self.zeroconf.add_listener(handler, None)
        
    def _run(self):
        """Main loop that sends queries"""
        while self.running:
            try:
                # Send query for _scream._udp services
                if self.zeroconf:
                    self._send_query("_scream._udp.local.")
                
                # Wait for next interval
                #logger.debug(f"Sleeping for {self.interval} seconds")
                time.sleep(self.interval)

            except Exception as e:
                if self.running:
                    #logger.exception("Error in mDNS refresh loop")
                    time.sleep(1)  # Avoid tight loop on error

"""mDNS pinger for discovering and syncing settings"""
import json
import logging
import threading
import time
from typing import Any, Dict, List, Optional

from zeroconf import (DNSIncoming, DNSOutgoing, DNSQuestion, DNSRecord,
                      RecordUpdateListener, Zeroconf, current_time_millis)

from screamrouter.screamrouter_types.annotations import IPAddressType

# DNS record types and flags
DNS_TYPE_TXT = 16  # TXT record type
DNS_FLAGS_QR_QUERY = 0x0000  # Standard query

logger = logging.getLogger(__name__)

class AudioSettings:
    """Class to hold audio settings"""
    def __init__(
        self, 
        receiver_id: str,
        ip: IPAddressType,
        bit_depth: int = 32, 
        sample_rate: int = 48000, 
        channels: int = 8, 
        channel_layout: str = "7.1",
    ):
        """Initialize AudioSettings
        
        Args:
            bit_depth: Audio bit depth (default: 32)
            sample_rate: Audio sample rate in Hz (default: 48000)
            channels: Number of audio channels (default: 8)
            channel_layout: Channel layout string (default: "7.1")
        """
        self.bit_depth = bit_depth
        self.sample_rate = sample_rate
        self.channels = channels
        self.channel_layout = channel_layout
        self.receiver_id = receiver_id
        self.ip = ip
    
    def __eq__(self, other):
        """Check if two AudioSettings objects are equal"""
        if not isinstance(other, AudioSettings):
            return False
        return (
            self.bit_depth == other.bit_depth and
            self.sample_rate == other.sample_rate and
            self.channels == other.channels and
            self.channel_layout == other.channel_layout
        )
    
    def __str__(self):
        """String representation of AudioSettings"""
        return (
            f"AudioSettings(bit_depth={self.bit_depth}, "
            f"sample_rate={self.sample_rate}, "
            f"channels={self.channels}, "
            f"channel_layout={self.channel_layout})"
        )

class SourceSettings:
    """Class to hold source settings"""
    def __init__(
        self,
        source_id: str,
        ip: IPAddressType,
        tag: Optional[str] = None,
        vnc_ip: Optional[IPAddressType] = None,
        vnc_port: Optional[int] = None
    ):
        """Initialize SourceSettings
        
        Args:
            source_id: Unique identifier for the source
            ip: IP address of the source
            tag: Optional tag for the source
            vnc_ip: Optional VNC IP address
            vnc_port: Optional VNC port
        """
        self.source_id = source_id
        self.ip = ip
        self.tag = tag
        self.vnc_ip = vnc_ip
        self.vnc_port = vnc_port
    
    def __eq__(self, other):
        """Check if two SourceSettings objects are equal"""
        if not isinstance(other, SourceSettings):
            return False
        return (
            self.source_id == other.source_id and
            self.ip == other.ip and
            self.tag == other.tag and
            self.vnc_ip == other.vnc_ip and
            self.vnc_port == other.vnc_port
        )
    
    def __str__(self):
        """String representation of SourceSettings"""
        return (
            f"SourceSettings(source_id={self.source_id}, "
            f"ip={self.ip}, "
            f"tag={self.tag}, "
            f"vnc_ip={self.vnc_ip}, "
            f"vnc_port={self.vnc_port})"
        )

class MDNSSettingsPinger:
    """Discovers and syncs settings using mDNS TXT records"""
    def __init__(self, configuration_manager=None, interval: float = 5.0):
        """Initialize MDNSSettingsPinger
        
        Args:
            configuration_manager: Reference to the ConfigurationManager
            interval: How often to refresh services in seconds
        """
        self.interval = interval
        self.running = False
        self.thread: Optional[threading.Thread] = None
        self.zeroconf: Optional[Zeroconf] = None
        self.configuration_manager = configuration_manager
        self.last_sink_settings: Dict[str, Dict[str, Any]] = {}
        self.last_source_settings: Dict[str, Dict[str, Any]] = {}
        self.current_handler = None
        self.known_sink_settings: List[AudioSettings] = []
        self.known_source_settings: List[SourceSettings] = []
        
        logger.debug("Initializing Zeroconf mDNS Settings handler")
        
    def start(self):
        """Start the mDNS settings discovery"""
        if self.running:
            return
            
        self.running = True
        self.zeroconf = Zeroconf()
        
        # Start refresh thread
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        
        logger.info("mDNS settings discovery started")
        
    def stop(self):
        """Stop the mDNS settings discovery"""
        self.running = False
        if self.thread:
            self.thread.join()
        if self.zeroconf:
            self.zeroconf.close()
            self.zeroconf = None
        logger.info("mDNS settings discovery stopped")
    
    def _process_sink_txt_record(self, record):
        """Process a sink TXT record and update settings if needed"""
        if not hasattr(record, 'text'):
            return
            
        try:
            # Convert bytes to string and parse settings
            txt_data = record.text
            if isinstance(txt_data, bytes):
                txt_data = txt_data.decode('utf-8')
            if txt_data[:1] == "P":
                txt_data = txt_data[1:]
                
            # Parse the TXT record content
            settings_str = txt_data.strip('"')
            settings_parts = settings_str.split(';')
            settings = {}
            
            for part in settings_parts:
                if '=' in part:
                    key, value = part.split('=', 1)
                    settings[key.strip()] = value.strip()
            
            # Check if we have a config_id
            if 'id' in settings:
                config_id = settings['id']
                
                # Check if settings have changed since last time
                if (config_id in self.last_sink_settings and
                    self.last_sink_settings[config_id] == settings):
                    return
                    
                # Store the latest settings
                self.last_sink_settings[config_id] = settings
                
                # Create AudioSettings object and add/update in known_sink_settings
                if 'ip' in settings:
                    audio_settings = AudioSettings(receiver_id=settings.get('id', None),
                        ip=settings.get('ip', None),
                        bit_depth=int(settings.get('bit_depth', 32)),
                        sample_rate=int(settings.get('sample_rate', 48000)),
                        channels=int(settings.get('channels', 8)),
                        channel_layout=settings.get('channel_layout', '7.1'),
                    )
                    
                    # Check if we already have settings with this receiver_id
                    updated = False
                    for i, existing in enumerate(self.known_sink_settings):
                        if hasattr(existing, 'receiver_id') and existing.receiver_id == config_id:
                            # Update existing settings
                            self.known_sink_settings[i] = audio_settings
                            # Add receiver_id attribute to the audio_settings
                            audio_settings.receiver_id = config_id
                            logger.info(f"Updated settings for sink receiver_id: {config_id}")
                            updated = True
                            break
                    
                    if not updated:
                        # Add receiver_id attribute to the audio_settings
                        audio_settings.receiver_id = config_id
                        # Add new settings
                        self.known_sink_settings.append(audio_settings)
                        logger.info(f"Added new settings for sink receiver_id: {config_id}")
                    
                logger.info(f"Processed settings for sink config_id: {config_id}")
            else:
                logger.warning("Received sink settings TXT record without an ID")
                
        except Exception as e:
            logger.error(f"Error processing sink TXT record: {e}")
            
    def _process_source_txt_record(self, record):
        """Process a source TXT record and update settings if needed"""
        if not hasattr(record, 'text'):
            return
            
        try:
            # Convert bytes to string and parse settings
            txt_data = record.text
            if isinstance(txt_data, bytes):
                txt_data = txt_data.decode('utf-8')

            if txt_data[:1] == "P":
                txt_data = txt_data[1:]
            print(txt_data)
                
            # Parse the TXT record content
            settings_str = txt_data.strip('"')
            settings_parts = settings_str.split(';')
            settings = {}
            
            for part in settings_parts:
                if '=' in part:
                    key, value = part.split('=', 1)
                    settings[key.strip()] = value.strip()
            
            # Check if we have a source_id
            if 'id' in settings:
                source_id = settings['id']
                
                # Check if settings have changed since last time
                if (source_id in self.last_source_settings and
                    self.last_source_settings[source_id] == settings):
                    return
                    
                # Store the latest settings
                self.last_source_settings[source_id] = settings
                
                # Create SourceSettings object and add/update in known_source_settings
                if 'ip' in settings:
                    source_settings = SourceSettings(
                        source_id=source_id,
                        ip=settings.get('ip', None),
                        tag=settings.get('tag', None),
                        vnc_ip=settings.get('vnc_ip', None),
                        vnc_port=int(settings.get('vnc_port', 5900)) if 'vnc_port' in settings else None
                    )
                    
                    # Check if we already have settings with this source_id
                    updated = False
                    for i, existing in enumerate(self.known_source_settings):
                        if existing.source_id == source_id:
                            # Update existing settings
                            self.known_source_settings[i] = source_settings
                            logger.info(f"Updated settings for source_id: {source_id}")
                            updated = True
                            break
                    
                    if not updated:
                        # Add new settings
                        self.known_source_settings.append(source_settings)
                        logger.info(f"Added new settings for source_id: {source_id}")
                    
                logger.info(f"Processed settings for source_id: {source_id}")
            else:
                logger.warning("Received source settings TXT record without an ID")
                
        except Exception as e:
            logger.error(f"Error processing source TXT record: {e}")
    
    def get_sink_settings_by_id(self, receiver_id: str) -> Optional[AudioSettings]:
        """Get sink settings by receiver ID
        
        Args:
            receiver_id: Receiver ID to look for
            
        Returns:
            AudioSettings or None if not found
        """
        for settings in self.known_sink_settings:
            if hasattr(settings, 'receiver_id') and settings.receiver_id == receiver_id:
                return settings
        return None
    
    def get_all_sink_settings(self) -> List[AudioSettings]:
        """Get all known sink settings
        
        Returns:
            List of AudioSettings objects
        """
        return self.known_sink_settings.copy()
        
    def get_source_settings_by_id(self, source_id: str) -> Optional[SourceSettings]:
        """Get source settings by source ID
        
        Args:
            source_id: Source ID to look for
            
        Returns:
            SourceSettings or None if not found
        """
        for settings in self.known_source_settings:
            if settings.source_id == source_id:
                return settings
        return None
    
    def get_all_source_settings(self) -> List[SourceSettings]:
        """Get all known source settings
        
        Returns:
            List of SourceSettings objects
        """
        return self.known_source_settings.copy()
        
    def get_all_settings(self) -> List[AudioSettings]:
        """Get all known sink settings (legacy method for backward compatibility)
        
        Returns:
            List of AudioSettings objects
        """
        return self.known_sink_settings.copy()
    
    def _send_queries(self) -> None:
        """Send mDNS queries for sink and source settings TXT records"""
        if not self.zeroconf:
            return
            
        # Create and send queries
        out = DNSOutgoing(DNS_FLAGS_QR_QUERY)
        out.add_question(DNSQuestion("sink.settings.screamrouter.local.", DNS_TYPE_TXT, 1))
        out.add_question(DNSQuestion("source.settings.screamrouter.local.", DNS_TYPE_TXT, 1))
        self.zeroconf.send(out)
        
        # Wait a bit for responses to arrive
        time.sleep(0.1)
        
        # Register a handler for incoming packets
        class ResponseHandler(RecordUpdateListener):
            def __init__(self, pinger):
                self.pinger = pinger
                
            def update_record(self, zeroconf, now, record):
                if record.type == DNS_TYPE_TXT:  # TXT record
                    name = record.name.lower()
                    if "sink.settings.screamrouter.local" in name:
                        logger.debug(f"Found sink TXT record for {name}")
                        self.pinger._process_sink_txt_record(record)
                    elif "source.settings.screamrouter.local" in name:
                        logger.debug(f"Found source TXT record for {name}")
                        self.pinger._process_source_txt_record(record)
        
        # Remove previous handler if it exists
        if self.current_handler:
            self.zeroconf.remove_listener(self.current_handler)
            
        # Create and register the handler
        handler = ResponseHandler(self)
        self.zeroconf.add_listener(handler, None)
        self.current_handler = handler
        
    def _run(self):
        """Main loop that sends queries"""
        while self.running:
            try:
                # Send queries for sink and source settings
                if self.zeroconf:
                    self._send_queries()
                
                # Wait for next interval
                #logger.debug(f"Sleeping for {self.interval} seconds")
                time.sleep(self.interval)

            except Exception as e:
                if self.running:
                    logger.exception("Error in mDNS settings refresh loop")
                    time.sleep(1)  # Avoid tight loop on error

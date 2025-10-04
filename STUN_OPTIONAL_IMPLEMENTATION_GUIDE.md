
# Implementation Guide: Making STUN Servers Optional

## Quick Start Implementation

This guide provides ready-to-use code for implementing optional STUN server support in ScreamRouter.

## 1. Frontend Implementation (TypeScript)

### A. Create ICEConfigurationManager.ts

```typescript
// screamrouter-react/src/lib/webrtc/ICEConfigurationManager.ts

export type NetworkType = 'local' | 'nat' | 'symmetric-nat' | 'unknown';
export type ICEMode = 'auto' | 'local-only' | 'stun-required' | 'full';

export interface SmartICEConfig {
  mode: ICEMode;
  localNetworkPriority: boolean;
  stunServers?: RTCIceServer[];
  turnServers?: RTCIceServer[];
  candidateTimeout?: number;
  networkDetection?: boolean;
  localSubnets?: string[];
}

export interface PeerInfo {
  sinkId: string;
  clientIP?: string;
  serverIP?: string;
}

export class ICEConfigurationManager {
  private defaultConfig: SmartICEConfig = {
    mode: 'auto',
    localNetworkPriority: true,
    stunServers: [
      { urls: 'stun:stun.l.google.com:19302' },
      { urls: 'stun:stun1.l.google.com:19302' }
    ],
    turnServers: [],
    candidateTimeout: 5000,
    networkDetection: true,
    localSubnets: [
      '192.168.0.0/16',
      '10.0.0.0/8',
      '172.16.0.0/12',
      'fd00::/8'  // IPv6 local
    ]
  };

  constructor(config?: Partial<SmartICEConfig>) {
    if (config) {
      this.defaultConfig = { ...this.defaultConfig, ...config };
    }
  }

  /**
   * Generate ICE configuration based on network conditions
   */
  async generateICEConfig(peerInfo?: PeerInfo): Promise<RTCConfiguration> {
    const mode = this.defaultConfig.mode;
    
    // Handle explicit modes
    if (mode === 'local-only') {
      return this.getLocalOnlyConfig();
    }
    
    if (mode === 'stun-required') {
      return this.getSTUNRequiredConfig();
    }
    
    if (mode === 'full') {
      return this.getFullICEConfig();
    }
    
    // Auto mode - detect network and decide
    if (mode === 'auto' && this.defaultConfig.networkDetection) {
      const isLocal = await this.detectLocalNetwork(peerInfo);
      
      if (isLocal) {
        console.log('[ICEConfig] Local network detected, using host candidates only');
        return this.getLocalOnlyConfig();
      }
    }
    
    // Default to STUN configuration
    return this.getSTUNConfig();
  }

  /**
   * Configuration for local network only (no STUN/TURN)
   */
  private getLocalOnlyConfig(): RTCConfiguration {
    return {
      iceServers: [],
      iceCandidatePoolSize: 0,
      iceTransportPolicy: 'all',
      bundlePolicy: 'max-bundle',
      rtcpMuxPolicy: 'require'
    };
  }

  /**
   * Configuration with STUN servers only
   */
  private getSTUNConfig(): RTCConfiguration {
    return {
      iceServers: this.defaultConfig.stunServers || [],
      iceCandidatePoolSize: 10,
      iceTransportPolicy: 'all',
      bundlePolicy: 'max-bundle',
      rtcpMuxPolicy: 'require'
    };
  }

  /**
   * Configuration that requires STUN (fails if unavailable)
   */
  private getSTUNRequiredConfig(): RTCConfiguration {
    if (!this.defaultConfig.stunServers?.length) {
      throw new Error('STUN servers required but none configured');
    }
    
    return {
      iceServers: this.defaultConfig.stunServers,
      iceCandidatePoolSize: 10,
      iceTransportPolicy: 'all',
      bundlePolicy: 'max-bundle',
      rtcpMuxPolicy: 'require'
    };
  }

  /**
   * Full configuration with STUN and TURN
   */
  private getFullICEConfig(): RTCConfiguration {
    const servers: RTCIceServer[] = [];
    
    if (this.defaultConfig.stunServers?.length) {
      servers.push(...this.defaultConfig.stunServers);
    }
    
    if (this.defaultConfig.turnServers?.length) {
      servers.push(...this.defaultConfig.turnServers);
    }
    
    return {
      iceServers: servers,
      iceCandidatePoolSize: 10,
      iceTransportPolicy: 'all',
      bundlePolicy: 'max-bundle',
      rtcpMuxPolicy: 'require'
    };
  }

  /**
   * Detect if connection is on local network
   */
  private async detectLocalNetwork(peerInfo?: PeerInfo): Promise<boolean> {
    // Check if we have peer IP information
    if (!peerInfo?.clientIP) {
      return false;
    }
    
    // Check if IP is in local subnet
    return this.isLocalIP(peerInfo.clientIP);
  }

  /**
   * Check if IP address is in local network range
   */
  private isLocalIP(ip: string): boolean {
    // Handle IPv4
    if (ip.includes('.')) {
      const parts = ip.split('.').map(Number);
      
      // Check common private IP ranges (RFC 1918)
      if (parts[0] === 10) return true;  // 10.0.0.0/8
      if (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) return true;  // 172.16.0.0/12
      if (parts[0] === 192 && parts[1] === 168) return true;  // 192.168.0.0/16
      if (parts[0] === 127) return true;  // 127.0.0.0/8 (loopback)
    }
    
    // Handle IPv6
    if (ip.includes(':')) {
      if (ip.startsWith('fe80:')) return true;  // Link-local
      if (ip.startsWith('fd')) return true;  // Unique local
      if (ip === '::1') return true;  // Loopback
    }
    
    return false;
  }

  /**
   * Update configuration at runtime
   */
  updateConfig(config: Partial<SmartICEConfig>): void {
    this.defaultConfig = { ...this.defaultConfig, ...config };
  }

  /**
   * Get current configuration
   */
  getConfig(): SmartICEConfig {
    return { ...this.defaultConfig };
  }
}
```

### B. Create CandidatePrioritizer.ts

```typescript
// screamrouter-react/src/lib/webrtc/CandidatePrioritizer.ts

export interface CandidatePriority {
  host: number;
  srflx: number;
  prflx: number;
  relay: number;
}

export class CandidatePrioritizer {
  private priorities: CandidatePriority = {
    host: 126,   // Highest priority for local candidates
    srflx: 100,  // Server reflexive (STUN)
    prflx: 110,  // Peer reflexive
    relay: 0     // Lowest priority for TURN
  };

  constructor(customPriorities?: Partial<CandidatePriority>) {
    if (customPriorities) {
      this.priorities = { ...this.priorities, ...customPriorities };
    }
  }

  /**
   * Adjust candidate priority based on type
   */
  adjustCandidatePriority(candidate: RTCIceCandidate): RTCIceCandidate {
    const type = candidate.type as keyof CandidatePriority;
    const priority = this.priorities[type];
    
    if (priority !== undefined) {
      // Create a modified candidate with adjusted priority
      const modifiedCandidate = {
        ...candidate,
        priority: this.calculatePriority(priority, candidate)
      };
      
      return new RTCIceCandidate(modifiedCandidate);
    }
    
    return candidate;
  }

  /**
   * Calculate priority value (RFC 5245)
   */
  private calculatePriority(typePref: number, candidate: RTCIceCandidate): number {
    const localPref = 65535;  // Maximum local preference
    const componentId = 1;    // RTP component
    
    // Priority = (2^24 * type preference) + (2^8 * local preference) + (256 - component ID)
    return (Math.pow(2, 24) * typePref) + (Math.pow(2, 8) * localPref) + (256 - componentId);
  }

  /**
   * Filter candidates based on network conditions
   */
  filterCandidates(
    candidates: RTCIceCandidate[],
    preferLocal: boolean = true
  ): RTCIceCandidate[] {
    if (preferLocal) {
      // Sort by type priority
      return candidates.sort((a, b) => {
        const aPriority = this.priorities[a.type as keyof CandidatePriority] || 0;
        const bPriority = this.priorities[b.type as keyof CandidatePriority] || 0;
        return bPriority - aPriority;
      });
    }
    
    return candidates;
  }

  /**
   * Check if candidate is local
   */
  isLocalCandidate(candidate: RTCIceCandidate): boolean {
    return candidate.type === 'host';
  }

  /**
   * Get readable candidate info for debugging
   */
  getCandidateInfo(candidate: RTCIceCandidate): string {
    return `${candidate.type} - ${candidate.protocol} - ${candidate.address}:${candidate.port}`;
  }
}
```

### C. Update ConnectionManager.ts

```typescript
// screamrouter-react/src/lib/webrtc/ConnectionManager.ts (modified sections)

import { ICEConfigurationManager, PeerInfo } from './ICEConfigurationManager';
import { CandidatePrioritizer } from './CandidatePrioritizer';

export class ConnectionManager {
  private iceConfigManager: ICEConfigurationManager;
  private candidatePrioritizer: CandidatePrioritizer;
  
  constructor(
    whepClient: WHEPClient,
    candidateManager: ICECandidateManager,
    heartbeatManager: HeartbeatManager,
    config?: ConnectionConfig,
    callbacks?: ConnectionCallbacks
  ) {
    // ... existing initialization ...
    
    // Initialize new managers
    this.iceConfigManager = new ICEConfigurationManager({
      mode: config?.iceMode || 'auto',
      stunServers: config?.iceServers?.filter(s => s.urls.toString().includes('stun')),
      turnServers: config?.iceServers?.filter(s => s.urls.toString().includes('turn')),
      localNetworkPriority: config?.localNetworkPriority ?? true
    });
    
    this.candidatePrioritizer = new CandidatePrioritizer({
      host: 126,
      srflx: 100,
      prflx: 110,
      relay: 0
    });
  }

  async connect(sinkId: string): Promise<MediaStream> {
    console.log(`[ConnectionManager] Connecting to sink ${sinkId}`);
    
    // Disconnect existing connection if any
    if (this.connections.has(sinkId)) {
      console.log(`[ConnectionManager] Disconnecting existing connection to ${sinkId}`);
      await this.disconnect(sinkId);
    }
    
    // Generate dynamic ICE configuration
    const peerInfo: PeerInfo = {
      sinkId,
      clientIP: await this.detectClientIP()
    };
    
    const iceConfig = await this.iceConfigManager.generateICEConfig(peerInfo);
    console.log(`[ConnectionManager] Using ICE configuration:`, {
      servers: iceConfig.iceServers?.length || 0,
      policy: iceConfig.iceTransportPolicy
    });
    
    const connection: ActiveConnection = {
      sinkId,
      pc: new RTCPeerConnection(iceConfig),
      session: null,
      state: 'connecting',
      stream: null,
      reconnectAttempts: 0,
    };
    
    this.connections.set(sinkId, connection);
    this.updateConnectionState(sinkId, 'connecting');
    
    try {
      // Set up peer connection handlers with candidate prioritization
      this.setupPeerConnectionHandlers(connection);
      
      // ... rest of connection logic ...
    } catch (error) {
      console.error(`[ConnectionManager] Failed to connect to ${sinkId}:`, error);
      
      // Try fallback strategies if initial connection fails
      if (this.config.enableFallback) {
        return await this.connectWithFallback(sinkId);
      }
      
      await this.handleConnectionFailure(connection, error as Error);
      throw error;
    }
  }

  private setupPeerConnectionHandlers(connection: ActiveConnection): void {
    const pc = connection.pc;
    const sinkId = connection.sinkId;
    
    // Handle ICE candidates with prioritization
    pc.onicecandidate = (event) => {
      if (event.candidate) {
        // Log candidate for debugging
        console.log(`[ConnectionManager] Local ICE candidate:`, 
          this.candidatePrioritizer.getCandidateInfo(event.candidate));
        
        // Adjust priority if needed
        const adjustedCandidate = this.candidatePrioritizer.adjustCandidatePriority(event.candidate);
        
        if (connection.session) {
          this.whepClient.sendCandidate(connection.session, adjustedCandidate).catch(error =>
            console.error(`[ConnectionManager] Failed to send ICE candidate:`, error)
          );
        }
      }
    };
    
    // ... rest of existing handlers ...
  }

  /**
   * Try connection with fallback strategies
   */
  private async connectWithFallback(sinkId: string): Promise<MediaStream> {
    const strategies = [
      { name: 'local-only', mode: 'local-only' as const },
      { name: 'with-stun', mode: 'auto' as const },
      { name: 'full-ice', mode: 'full' as const }
    ];
    
    for (const strategy of strategies) {
      try {
        console.log(`[ConnectionManager] Attempting ${strategy.name} strategy`);
        
        // Update ICE configuration for this strategy
        this.iceConfigManager.updateConfig({ mode: strategy.mode });
        
        // Attempt connection
        return await this.connect(sinkId);
      } catch (error) {
        console.warn(`[ConnectionManager] ${strategy.name} strategy failed:`, error);
      }
    }
    
    throw new Error('All connection strategies failed');
  }

  /**
   * Detect client IP for network detection
   */
  private async detectClientIP(): Promise<string | undefined> {
    try {
      // Use WebRTC to detect local IP
      const pc = new RTCPeerConnection({ iceServers: [] });
      pc.createDataChannel('');
      
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      
      return new Promise((resolve) => {
        pc.onicecandidate = (event) => {
          if (event.candidate?.type === 'host') {
            const ip = event.candidate.address;
            pc.close();
            resolve(ip || undefined);
          }
        };
        
        // Timeout after 1 second
        setTimeout(() => {
          pc.close();
          resolve(undefined);
        }, 1000);
      });
    } catch (error) {
      console.error('[ConnectionManager] Failed to detect client IP:', error);
      return undefined;
    }
  }
}
```

## 2. Backend Implementation (C++)

### A. Create ice_configuration.h

```cpp
// src/audio_engine/configuration/ice_configuration.h

#ifndef ICE_CONFIGURATION_H
#define ICE_CONFIGURATION_H

#include <string>
#include <vector>
#include <optional>

namespace screamrouter {
namespace audio {

enum class ICEMode {
    AUTO,
    LOCAL_ONLY,
    STUN_REQUIRED,
    FULL
};

struct ICEServer {
    std::string url;
    std::optional<std::string> username;
    std::optional<std::string> credential;
};

struct ICEConfiguration {
    ICEMode mode = ICEMode::AUTO;
    bool local_network_priority = true;
    std::vector<ICEServer> stun_servers;
    std::vector<ICEServer> turn_servers;
    bool enable_ice_tcp = false;
    std::optional<uint16_t> port_range_begin;
    std::optional<uint16_t> port_range_end;
    
    // Network detection
    bool network_detection = true;
    std::vector<std::string> local_subnets = {
        "192.168.0.0/16",
        "10.0.0.0/8",
        "172.16.0.0/12"
    };
};

class ICEConfigurationManager {
public:
    ICEConfigurationManager();
    explicit ICEConfigurationManager(const ICEConfiguration& config);
    
    rtc::Configuration generate_rtc_config(const std::string& client_ip = "") const;
    void update_config(const ICEConfiguration& config);
    ICEConfiguration get_config() const;
    
private:
    bool is_local_network(const std::string& ip) const;
    bool is_ip_in_subnet(const std::string& ip, const std::string& subnet) const;
    
    ICEConfiguration config_;
};

} // namespace audio
} // namespace screamrouter

#endif // ICE_CONFIGURATION_H
```

### B. Create ice_configuration.cpp

```cpp
// src/audio_engine/configuration/ice_configuration.cpp

#include "ice_configuration.h"
#include "../utils/cpp_logger.h"
#include <rtc/rtc.hpp>
#include <arpa/inet.h>
#include <sstream>

namespace screamrouter {
namespace audio {

ICEConfigurationManager::ICEConfigurationManager() {
    // Default configuration
    config_.stun_servers.push_back({"stun:stun.l.google.com:19302"});
}

ICEConfigurationManager::ICEConfigurationManager(const ICEConfiguration& config)
    : config_(config) {}

rtc::Configuration ICEConfigurationManager::generate_rtc_config(const std::string& client_ip) const {
    rtc::Configuration rtc_config;
    
    // Handle different modes
    switch (config_.mode) {
        case ICEMode::LOCAL_ONLY:
            LOG_CPP_INFO("[ICEConfig] Using local-only mode (no STUN/TURN)");
            // No ICE servers for local only
            break;
            
        case ICEMode::STUN_REQUIRED:
            LOG_CPP_INFO("[ICEConfig] Using STUN-required mode");
            if (config_.stun_servers.empty()) {
                throw std::runtime_error("STUN servers required but none configured");
            }
            for (const auto& server : config_.stun_servers) {
                rtc_config.iceServers.emplace_back(server.url);
            }
            break;
            
        case ICEMode::FULL:
            LOG_CPP_INFO("[ICEConfig] Using full ICE mode (STUN + TURN)");
            for (const auto& server : config_.stun_servers) {
                rtc_config.iceServers.emplace_back(server.url);
            }
            for (const auto& server : config_.turn_servers) {
                if (server.username && server.credential) {
                    rtc_config.iceServers.emplace_back(
                        server.url + "?username=" + *server.username + 
                        "&credential=" + *server.credential
                    );
                } else {
                    rtc_config.iceServers.emplace_back(server.url);
                }
            }
            break;
            
        case ICEMode::AUTO:
        default:
            LOG_CPP_INFO("[ICEConfig] Using auto mode");
            
            // Check if client is on local network
            if (config_.network_detection && !client_ip.empty()) {
                if (is_local_network(client_ip)) {
                    LOG_CPP_INFO("[ICEConfig] Client IP %s is on local network, skipping STUN/TURN", 
                               client_ip.c_str());
                    // No ICE servers for local network
                    break;
                }
            }
            
            // Fall back to STUN servers
            LOG_CPP_INFO("[ICEConfig] Using STUN servers for non-local connection");
            for (const auto& server : config_.stun_servers) {
                rtc_config.iceServers.emplace_back(server.url);
            }
            break;
    }
    
    // Apply additional configuration
    rtc_config.disableAutoNegotiation = true;
    rtc_config.enableIceTcp = config_.enable_ice_tcp;
    
    if (config_.port_range_begin && config_.port_range_end) {
        rtc_config.portRangeBegin = *config_.port_range_begin;
        rtc_config.portRangeEnd = *config_.port_range_end;
    }
    
    LOG_CPP_INFO("[ICEConfig] Generated RTC config with %zu ICE servers", 
               rtc_config.iceServers.size());
    
    return rtc_config;
}

bool ICEConfigurationManager::is_local_network(const std::string& ip) const {
    // Check IPv4 private ranges
    if (ip.find('.') != std::string::npos) {
        // Parse IPv4 address
        struct sockaddr_in sa;
        if (inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) == 1) {
            uint32_t addr = ntohl(sa.sin_addr.s_addr);
            
            // Check private IP ranges (RFC 1918)
            if ((addr & 0xFF000000) == 0x0A000000) return true;  // 10.0.0.0/8
            if ((addr & 0xFFF00000) == 0xAC100000) return true;  // 172.16.0.0/12
            if ((addr & 0xFFFF0000) == 0xC0A80000) return true;  // 192.168.0.0/16
            if ((addr & 0xFF000000) == 0x7F000000) return true;  // 127.0.0.0/8
        }
    }
    
    // Check IPv6 local addresses
    if (ip.find(':') != std::string::npos) {
        if (ip.substr(0, 4) == "fe80") return true;  // Link-local
        if (ip.substr(0, 2) == "fd") return true;    // Unique local
        if (ip == "::1") return true;                // Loopback
    }
    
    // Check against configured subnets
    for (const auto& subnet : config_.local_subnets) {
        if (is_ip_in_subnet(ip, subnet)) {
            return true;
        }
    }
    
    return false;
}

bool ICEConfigurationManager::is_ip_in_subnet(const std::string& ip, const std::string& subnet) const {
    // Simple subnet check (can be enhanced with proper CIDR parsing)
    size_t slash_pos = subnet.find('/');
    if (slash_pos == std::string::npos) {
        return false;
    }
    
    std::string subnet_base = subnet.substr(0, slash_pos);
    int prefix_len = std::stoi(subnet.substr(slash_pos + 1));
    
    // For simplicity, just check if IP starts with subnet base
    // In production, implement proper CIDR matching
    return ip.substr(0, subnet_base.length()) == subnet_base;
}

void ICEConfigurationManager::update_config(const ICEConfiguration& config) {
    config_ = config;
    LOG_CPP_INFO("[ICEConfig] Configuration updated");
}

ICEConfiguration ICEConfigurationManager::get_config() const {
    return config_;
}

} // namespace audio
} // namespace screamrouter
```

### C. Update webrtc_sender.cpp

```cpp
// src/audio_engine/senders/webrtc/webrtc_sender.cpp (modified sections)

#include "ice_configuration.h"

class WebRtcSender {
private:
    ICEConfigurationManager ice_config_manager_;
    std::string client_ip_;
    
public:
    WebRtcSender(
        const SinkMixerConfig& config,
        std::string offer_sdp,
        std::function<void(const std::string& sdp)> on_local_description_callback,
        std::function<void(const std::string& candidate, const std::string& sdpMid)> on_ice_candidate_callback,
        const std::string& client_ip = "")
        : config_(config),
          offer_sdp_(std::move(offer_sdp)),
          on_local_description_callback_(on_local_description_callback),
          on_ice_candidate_callback_(on_ice_candidate_callback),
          client_ip_(client_ip),
          state_(rtc::PeerConnection::State::New),
          audio_track_(nullptr),
          current_timestamp_(0) {
        
        LOG_CPP_INFO("[WebRtcSender] Created for sink: %s, client IP: %s", 
                   config_.sink_id.c_str(), client_ip_.c_str());
        
        // Initialize ICE configuration
        initialize_ice_config();
        initialize_opus_encoder();
    }
    
    void initialize_ice_config() {
        // Load configuration from settings or use defaults
        ICEConfiguration ice_config;
        
        // Check environment variable for mode
        const char* ice_mode_env = std::getenv("SCREAMROUTER_ICE_MODE");
        if (ice_mode_env) {
            std::string mode_str(ice_mode_env);
            if (mode_str == "local-only") {
                ice_config.mode = ICEMode::LOCAL_ONLY;
            } else if (mode_str == "stun-required") {
                ice_config.mode = ICEMode::STUN_REQUIRED;
            } else if (mode_str == "full") {
                ice_config.mode = ICEMode::FULL;
            } else {
                ice_config.mode = ICEMode::AUTO;
            }
        }
        
        // Configure STUN servers
        const char* stun_servers_env = std::getenv("SCREAMROUTER_STUN_SERVERS");
        if (stun_servers_env) {
            ice_config.stun_servers.clear();
            std::stringstream ss(stun_servers_env);
            std::string server;
            while (std::getline(ss, server, ',')) {
                ice_config.stun_servers.push_back({server});
            }
        }
        
        // Configure TURN servers
        const char* turn_server_env = std::getenv("SCREAMROUTER_TURN_SERVER");
        if (turn_server_env) {
            const char* turn_user = std::getenv("SCREAMROUTER_TURN_USER");
            const char* turn_pass = std::getenv("SCREAMROUTER_TURN_PASS");
            
            ICEServer turn_server;
            turn_server.url = turn_server_env;
            if (turn_user) turn_server.username = turn_user;
            if (turn_pass) turn_server.credential = turn_pass;
            
            ice_config.turn_servers.push_back(turn_server);
        }
        
        ice_config_manager_ = ICEConfigurationManager(ice_config);
    }
    
    void setup_peer_connection() {
        // Generate RTC configuration based on client IP
        rtc::Configuration rtc_config = ice_config_manager_.generate_rtc_config(client_ip_);
        
        LOG_CPP_INFO("[WebRtcSender:%s] Using %zu ICE servers", 
                   config_.sink_id.c_str(), rtc_config.iceServers.size());
        
        peer_connection_ = std::make_unique<rtc::PeerConnection>(rtc_config);
        
        // ... rest of existing setup code ...
    }
};
```

## 3. Configuration File Support

### A. Create webrtc_config.yaml

```yaml
# config/webrtc_config.yaml

webrtc:
  ice:
    # Mode: auto | local-only | stun-required | full
    mode: "auto"
    
    # Prioritize local network connections
    local_network_priority: true
    
    # Timeout for gathering host candidates (ms)
    host_candidate_timeout: 2000
    
    # STUN servers
    stun_servers:
      - url: "stun:stun.l.google.com:19302"
        enabled: true
      - url: "stun:stun1.l.google.com:19302"
        enabled: true
    
    # TURN servers (optional)
    turn_servers:
      - url: "turn:192.168.3.201:3478"
        username: "screamrouter"
        credential: "screamrouter"
        enabled: false
    
    # Network detection settings
    network_detection:
      enabled: true
      # Local subnets to detect
      local_subnets:
        - "192.168.0.0/16"
        - "10.0.0.0/8"
        - "172.16.0.0/12"
        - "fd00::/8"  # IPv6 unique local
    
    # Port range for host candidates
    port_range:
      begin: 10000
      end: 10100
    
    # Enable TCP ICE candidates
    enable_tcp: false
    
  # Connection fallback strategies
  fallback:
    enabled: true
    strategies:
      - name: "local-only"
        timeout: 5000
      - name: "with-stun"
        timeout: 10000
      - name: "full-ice"
        timeout: 15000
```

### B. Configuration Loader (Python)

```python
# src/configuration/webrtc_config.py

import yaml
import os
from typing import Dict, Any, Optional, List
from dataclasses import dataclass, field

@dataclass
class STUNServer:
    url: str
    enabled: bool = True

@dataclass
class TURNServer:
    url: str
    username: Optional[str] = None
    credential: Optional[str] = None
    enabled: bool = False

@dataclass
class NetworkDetection:
    enabled: bool = True
    local_subnets: List[str] = field(default_factory
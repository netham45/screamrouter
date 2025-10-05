# Migration Strategy: Transitioning to Optional STUN Configuration

## Overview

This document outlines the migration strategy for transitioning existing ScreamRouter deployments to support optional STUN server configuration while maintaining backward compatibility.

## Migration Principles

1. **Zero Downtime**: Changes must not disrupt existing connections
2. **Backward Compatibility**: Existing configurations must continue to work
3. **Gradual Rollout**: Feature flags enable controlled deployment
4. **Rollback Capability**: Easy reversion to previous behavior if needed
5. **Clear Communication**: Users informed of changes and benefits

## Phase 1: Preparation (Week 1)

### 1.1 Code Preparation
- [ ] Create feature branch: `feature/optional-stun-support`
- [ ] Implement ICEConfigurationManager classes
- [ ] Add configuration schema support
- [ ] Create unit tests for new components

### 1.2 Environment Setup
```bash
# Add feature flags to environment
export SCREAMROUTER_ICE_MODE="auto"
export SCREAMROUTER_ENABLE_LOCAL_PRIORITY="true"
export SCREAMROUTER_LEGACY_ICE_MODE="false"
```

### 1.3 Configuration Templates
Create migration configuration templates:

```yaml
# config/migration/phase1.yaml
webrtc:
  ice:
    mode: "auto"  # Start with auto mode
    legacy_mode: true  # Keep legacy behavior initially
    stun_servers:
      - url: "stun:stun.l.google.com:19302"
    migration:
      enable_new_features: false
      log_migration_events: true
```

## Phase 2: Soft Launch (Week 2)

### 2.1 Feature Flag Implementation

```typescript
// src/lib/webrtc/FeatureFlags.ts
export class FeatureFlags {
  private static flags = {
    USE_SMART_ICE: process.env.REACT_APP_USE_SMART_ICE === 'true',
    ENABLE_LOCAL_PRIORITY: process.env.REACT_APP_ENABLE_LOCAL_PRIORITY === 'true',
    USE_LEGACY_ICE: process.env.REACT_APP_USE_LEGACY_ICE !== 'false'
  };

  static isSmartICEEnabled(): boolean {
    return this.flags.USE_SMART_ICE && !this.flags.USE_LEGACY_ICE;
  }

  static shouldPrioritizeLocal(): boolean {
    return this.flags.ENABLE_LOCAL_PRIORITY;
  }
}
```

### 2.2 Dual-Mode Support

```typescript
// src/lib/webrtc/ConnectionManager.ts
export class ConnectionManager {
  async connect(sinkId: string): Promise<MediaStream> {
    let iceConfig: RTCConfiguration;
    
    if (FeatureFlags.isSmartICEEnabled()) {
      // New smart ICE configuration
      iceConfig = await this.iceConfigManager.generateICEConfig();
      console.log('[Migration] Using new smart ICE configuration');
    } else {
      // Legacy configuration
      iceConfig = this.getLegacyICEConfig();
      console.log('[Migration] Using legacy ICE configuration');
    }
    
    // Log migration metrics
    this.logMigrationMetrics('ice_config_used', {
      mode: FeatureFlags.isSmartICEEnabled() ? 'smart' : 'legacy',
      servers: iceConfig.iceServers?.length || 0
    });
    
    // ... rest of connection logic
  }
}
```

### 2.3 A/B Testing Setup

```python
# src/api/api_webrtc.py
class APIWebRTC:
    def __init__(self, app: APIRouter, audio_manager: AudioManager):
        # ... existing init ...
        self.ab_test_groups = self.initialize_ab_testing()
    
    def initialize_ab_testing(self):
        """Initialize A/B testing for ICE configuration"""
        return {
            'control': {'percentage': 70, 'config': 'legacy'},
            'treatment': {'percentage': 30, 'config': 'smart'}
        }
    
    async def whep_post(self, sink_id: str, request: Request):
        # Determine A/B test group
        client_ip = request.client.host
        test_group = self.get_ab_test_group(client_ip)
        
        # Log for analysis
        logger.info(f"Client {client_ip} assigned to {test_group} group")
        
        # Pass configuration hint to C++ layer
        ice_mode = "smart" if test_group == "treatment" else "legacy"
        # ... rest of implementation
```

## Phase 3: Gradual Rollout (Week 3-4)

### 3.1 Rollout Schedule

| Week | Percentage | Target Users | Rollback Threshold |
|------|------------|--------------|-------------------|
| 3.1  | 10%        | Beta testers | >5% failure rate  |
| 3.2  | 25%        | Power users  | >3% failure rate  |
| 3.3  | 50%        | Half users   | >2% failure rate  |
| 3.4  | 75%        | Most users   | >1% failure rate  |
| 4.1  | 100%       | All users    | >0.5% failure rate|

### 3.2 Monitoring Dashboard

```typescript
// src/monitoring/MigrationDashboard.ts
export interface MigrationMetrics {
  totalConnections: number;
  smartICEConnections: number;
  legacyICEConnections: number;
  localNetworkConnections: number;
  stunRequiredConnections: number;
  connectionSuccessRate: {
    smart: number;
    legacy: number;
  };
  averageConnectionTime: {
    smart: number;
    legacy: number;
  };
  failureReasons: Map<string, number>;
}

export class MigrationMonitor {
  async collectMetrics(): Promise<MigrationMetrics> {
    // Collect and aggregate metrics
    return {
      totalConnections: await this.getTotalConnections(),
      smartICEConnections: await this.getSmartICEConnections(),
      // ... etc
    };
  }
  
  async checkRollbackCriteria(): Promise<boolean> {
    const metrics = await this.collectMetrics();
    const smartFailureRate = 1 - metrics.connectionSuccessRate.smart;
    const threshold = this.getRollbackThreshold();
    
    if (smartFailureRate > threshold) {
      console.error(`Smart ICE failure rate ${smartFailureRate} exceeds threshold ${threshold}`);
      return true;  // Should rollback
    }
    
    return false;
  }
}
```

### 3.3 Rollback Procedure

```bash
#!/bin/bash
# scripts/rollback_ice_config.sh

echo "Rolling back to legacy ICE configuration..."

# Update environment variables
export SCREAMROUTER_ICE_MODE="legacy"
export SCREAMROUTER_USE_SMART_ICE="false"

# Update configuration files
cp config/backup/legacy_ice.yaml config/webrtc_config.yaml

# Restart services
systemctl restart screamrouter

# Notify monitoring
curl -X POST https://monitoring.example.com/rollback \
  -H "Content-Type: application/json" \
  -d '{"component": "ice_config", "reason": "high_failure_rate"}'

echo "Rollback complete"
```

## Phase 4: Full Migration (Week 5)

### 4.1 Legacy Deprecation Notice

```typescript
// src/lib/webrtc/LegacyDeprecation.ts
export class LegacyDeprecation {
  static checkAndNotify(): void {
    if (this.isUsingLegacyConfig()) {
      console.warn(
        '[DEPRECATION] Legacy ICE configuration will be removed in v3.0.0. ' +
        'Please migrate to the new smart ICE configuration. ' +
        'See: https://docs.screamrouter.com/migration/ice-config'
      );
      
      // Show user notification
      this.showUserNotification({
        title: 'ICE Configuration Update',
        message: 'A new, more efficient connection method is available. ' +
                'Would you like to enable it?',
        actions: [
          { label: 'Enable', action: () => this.enableSmartICE() },
          { label: 'Learn More', action: () => this.openDocumentation() },
          { label: 'Later', action: () => this.dismissNotification() }
        ]
      });
    }
  }
}
```

### 4.2 Configuration Migration Tool

```python
#!/usr/bin/env python3
# scripts/migrate_ice_config.py

import yaml
import json
import sys
from pathlib import Path

class ICEConfigMigrator:
    def __init__(self, config_path: str):
        self.config_path = Path(config_path)
        self.backup_path = self.config_path.with_suffix('.backup')
    
    def migrate(self):
        """Migrate legacy configuration to new format"""
        # Backup existing config
        self.backup_config()
        
        # Load current config
        config = self.load_config()
        
        # Migrate to new format
        new_config = self.transform_config(config)
        
        # Validate new config
        if not self.validate_config(new_config):
            print("Migration failed: Invalid configuration")
            self.restore_backup()
            return False
        
        # Save new config
        self.save_config(new_config)
        
        print(f"Successfully migrated configuration to {self.config_path}")
        return True
    
    def transform_config(self, old_config: dict) -> dict:
        """Transform legacy config to new format"""
        new_config = {
            'webrtc': {
                'ice': {
                    'mode': 'auto',
                    'local_network_priority': True,
                    'stun_servers': [],
                    'turn_servers': []
                }
            }
        }
        
        # Extract STUN servers from old config
        if 'iceServers' in old_config:
            for server in old_config['iceServers']:
                if 'stun:' in server.get('urls', ''):
                    new_config['webrtc']['ice']['stun_servers'].append({
                        'url': server['urls'],
                        'enabled': True
                    })
                elif 'turn:' in server.get('urls', ''):
                    new_config['webrtc']['ice']['turn_servers'].append({
                        'url': server['urls'],
                        'username': server.get('username'),
                        'credential': server.get('credential'),
                        'enabled': True
                    })
        
        return new_config
    
    def backup_config(self):
        """Create backup of existing configuration"""
        import shutil
        shutil.copy2(self.config_path, self.backup_path)
        print(f"Backed up configuration to {self.backup_path}")
    
    def restore_backup(self):
        """Restore configuration from backup"""
        import shutil
        shutil.copy2(self.backup_path, self.config_path)
        print(f"Restored configuration from {self.backup_path}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: migrate_ice_config.py <config_file>")
        sys.exit(1)
    
    migrator = ICEConfigMigrator(sys.argv[1])
    success = migrator.migrate()
    sys.exit(0 if success else 1)
```

## Phase 5: Cleanup (Week 6)

### 5.1 Remove Legacy Code

```typescript
// After full migration, remove legacy code
// Mark with @deprecated tags first, then remove after grace period

/**
 * @deprecated Since v2.5.0. Will be removed in v3.0.0.
 * Use ICEConfigurationManager instead.
 */
export function getLegacyICEConfig(): RTCConfiguration {
  console.warn('getLegacyICEConfig is deprecated');
  // ... legacy implementation
}
```

### 5.2 Documentation Updates

- [ ] Update README with new ICE configuration options
- [ ] Create migration guide for users
- [ ] Update API documentation
- [ ] Add troubleshooting guide for common issues
- [ ] Create video tutorial for configuration

## User Communication Plan

### Email Template
```markdown
Subject: Important Update: Improved Connection Reliability in ScreamRouter

Dear ScreamRouter User,

We're excited to announce an improvement to ScreamRouter's connection system that will:

✅ **Faster local network connections** - Connect 50% faster on your local network
✅ **Improved reliability** - No dependency on external STUN servers for local connections
✅ **Automatic optimization** - Smart detection of your network configuration

**What's changing?**
- ScreamRouter now intelligently detects when you're on a local network
- STUN servers are only used when necessary for remote connections
- Your existing configuration will continue to work

**Action required:** None! The update is automatic.

**Want to optimize further?**
Visit our [configuration guide](https://docs.screamrouter.com/ice-config) to learn about advanced options.

Best regards,
The ScreamRouter Team
```

## Success Metrics

### Key Performance Indicators (KPIs)

1. **Connection Success Rate**
   - Target: >99.5% for local networks
   - Target: >98% for remote connections

2. **Connection Time**
   - Target: <2 seconds for local networks
   - Target: <5 seconds for remote connections

3. **STUN Server Dependency**
   - Target: 0% for local network connections
   - Target: <50% for all connections

4. **User Satisfaction**
   - Target: <0.1% increase in support tickets
   - Target: >90% positive feedback

### Monitoring Queries

```sql
-- Connection success rate by configuration type
SELECT 
  config_type,
  COUNT(*) as total_attempts,
  SUM(CASE WHEN success = true THEN 1 ELSE 0 END) as successful,
  AVG(connection_time_ms) as avg_time_ms,
  (SUM(CASE WHEN success = true THEN 1 ELSE 0 END) * 100.0 / COUNT(*)) as success_rate
FROM connection_attempts
WHERE timestamp > NOW() - INTERVAL '24 hours'
GROUP BY config_type;

-- STUN server usage
SELECT 
  network_type,
  COUNT(*) as connections,
  SUM(CASE WHEN stun_used = true THEN 1 ELSE 0 END) as stun_required,
  (SUM(CASE WHEN stun_used = true THEN 1 ELSE 0 END) * 100.0 / COUNT(*)) as stun_percentage
FROM connection_metrics
WHERE timestamp > NOW() - INTERVAL '24 hours'
GROUP BY network_type;
```

## Risk Mitigation

### Identified Risks and Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Connection failures increase | High | Low | Feature flags, gradual rollout, monitoring |
| Firewall blocks local connections | Medium | Medium | Fallback to STUN, clear documentation |
| Performance regression | Medium | Low | Performance testing, metrics monitoring |
| User confusion | Low | Medium | Clear communication, documentation |
| Legacy system incompatibility | High | Low | Backward compatibility, dual-mode support |

## Rollout Checklist

### Pre-Rollout
- [ ] Code review completed
- [ ] Unit tests passing (>90% coverage)
- [ ] Integration tests passing
- [ ] Performance benchmarks acceptable
- [ ] Documentation updated
- [ ] Monitoring dashboard ready
- [ ] Rollback procedure tested
- [ ] Support team briefed

### During Rollout
- [ ] Feature flags configured
- [ ] A/B test groups assigned
- [ ] Metrics being collected
- [ ] Error rates monitored
- [ ] User feedback collected
- [ ] Support tickets tracked

### Post-Rollout
- [ ] Success metrics evaluated
- [ ] User feedback analyzed
- [ ] Performance impact assessed
- [ ] Documentation feedback incorporated
- [ ] Legacy code deprecation scheduled
- [ ] Lessons learned documented

## Timeline Summary

| Week | Phase | Key Activities |
|------|-------|----------------|
| 1 | Preparation | Code implementation, testing |
| 2 | Soft Launch | Feature flags, A/B testing setup |
| 3-4 | Gradual Rollout | Progressive deployment, monitoring |
| 5 | Full Migration | 100% deployment, legacy deprecation |
| 6 | Cleanup | Remove legacy code, documentation |

## Conclusion

This migration strategy ensures a smooth transition to optional STUN configuration while maintaining system stability and user satisfaction. The gradual rollout with comprehensive monitoring allows for quick detection and resolution of any issues that may arise.
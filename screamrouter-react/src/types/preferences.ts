export interface TutorialProgress {
  current_step: number;
  completed: boolean;
  completed_at: string | null;
  version: number;
  completed_steps: string[];
  completedSteps?: string[];
}

export interface Preferences {
  tutorial: TutorialProgress;
}

export type PreferencesUpdatePayload = Partial<{
  tutorial: Partial<TutorialProgress>;
}>;

export interface DiscoveredDevice {
  discovery_method: string;
  role: string;
  ip: string;
  port?: number | null;
  name?: string | null;
  tag?: string | null;
  properties: Record<string, unknown>;
  last_seen: string;
  device_type?: string | null;
}

export interface UnifiedDiscoverySnapshot {
  discovered_devices: DiscoveredDevice[];
  sink_settings: Array<Record<string, unknown>>;
  source_settings: Array<Record<string, unknown>>;
}

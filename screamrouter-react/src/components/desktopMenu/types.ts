/**
 * Type definitions specific to the DesktopMenu component.
 */
import { Source, Sink, Route } from '../../api/api';

/**
 * Enum for different menu levels in the DesktopMenu.
 */
export enum MenuLevel {
  Main = 0,
  Sources = 1,
  Sinks = 2,
  Routes = 3,
  AllSources = 4,
  AllSinks = 5,
  AllRoutes = 6,
  NowListening = 7,
  RecentlyUsed = 8, // Add new enum member
}

/**
 * Interface for actions specific to the DesktopMenu.
 */
export interface DesktopMenuActions {
  /**
   * Toggle star status for an item.
   */
  toggleStar: (type: 'sources' | 'sinks' | 'routes', name: string) => void;
  
  /**
   * Toggle enabled status for an item.
   */
  toggleEnabled: (type: 'sources' | 'sinks' | 'routes', name: string, enable: boolean) => void;
  
  /**
   * Update volume for a source, sink, or route.
   */
  updateVolume: (type: 'sources' | 'sinks' | 'routes', name: string, value: number) => void;
  
  /**
   * Update timeshift for a source, sink, or route.
   */
  updateTimeshift: (type: 'sources' | 'sinks' | 'routes', name: string, value: number) => void;
  
  /**
   * Show equalizer for an item.
   */
  showEqualizer: (show: boolean, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: Source | Sink | Route) => void;
  
  /**
   * Show VNC for a source.
   */
  showVNC: (show: boolean, source: Source) => void;
  
  /**
   * Toggle Primary Source.
   */
  toggleActiveSource: (name: string) => void;

  /**
   * Transcribe a sink.
   */
  transcribeSink: (ip: string) => void;
  
  /**
   * Listen to a sink.
   */
  listenToSink: (name: string | null) => void;
  
  /**
   * Visualize a sink.
   */
  visualizeSink: (name: string | null) => void;
  
  /**
   * Control a source (play, pause, etc.).
   */
  controlSource: (name: string, action: 'play' | 'pause' | 'prevtrack' | 'nexttrack') => void;
  
  /**
   * Navigate to an item.
   */
  navigate: (type: 'sources' | 'sinks' | 'routes', name: string) => void;
  
  /**
   * Show confirmation dialog before deleting an item.
   */
  confirmDelete: (type: 'sources' | 'sinks' | 'routes', name: string) => void;

  /**
   * Show speaker layout page for an item in a new window.
   */
  showSpeakerLayoutPage: (type: 'sources' | 'sinks' | 'routes', item: Source | Sink | Route) => void;
}

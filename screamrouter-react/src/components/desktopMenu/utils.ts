/**
 * Utility functions specific to the DesktopMenu component.
 */
import { Source, Sink, Route } from '../../api/api';
import ApiService from '../../api/api';
import { DesktopMenuActions } from './types';
import { addToRecents } from '../../utils/recents';

/**
 * Creates actions for the DesktopMenu component.
 * 
 * @param setStarredItems - Function to set starred items
 * @param setError - Function to set error message
 * @param onToggleActiveSource - Function to toggle Primary Source
 * @param onListenToSink - Function to listen to a sink
 * @param onVisualizeSink - Function to visualize a sink
 * @param navigateToItem - Function to navigate to an item
 * @returns DesktopMenuActions object
 */
export const createDesktopMenuActions = (
  setStarredItems: (type: 'sources' | 'sinks' | 'routes', setter: (prev: string[]) => string[]) => void,
  setError: (error: string | null) => void,
  onToggleActiveSource: (name: string) => void,
  onTranscribeSink: (ip: string) => void,
  onListenToSink: (name: string | null) => void,
  onVisualizeSink: (name: string | null) => void,
  navigateToItem: (type: 'sources' | 'sinks' | 'routes', name: string) => void
): DesktopMenuActions => {
  /**
   * Opens a URL in a new window.
   * 
   * @param url - URL to open
   * @param width - Window width
   * @param height - Window height
   */
  const openInNewWindow = (url: string, width: number = 800, height: number = 600) => {
    const left = (window.screen.width - width) / 2;
    const top = (window.screen.height - height) / 2;
    window.open(url, '_blank', `width=${width},height=${height},left=${left},top=${top}`);
  };

  return {
    toggleStar: (type: 'sources' | 'sinks' | 'routes', name: string) => {
      setStarredItems(type, (prev) => {
        if (prev.includes(name)) {
          return prev.filter(item => item !== name);
        } else {
          return [...prev, name];
        }
      });
      
      // Save to localStorage
      const key = `starred${type.charAt(0).toUpperCase() + type.slice(1)}`;
      try {
        const currentItems = JSON.parse(localStorage.getItem(key) || '[]');
        if (currentItems.includes(name)) {
          localStorage.setItem(key, JSON.stringify(currentItems.filter((item: string) => item !== name)));
        } else {
          localStorage.setItem(key, JSON.stringify([...currentItems, name]));
        }
      } catch (error) {
        console.error(`Error saving starred ${type} to localStorage:`, error);
        setError(`Error saving starred ${type}`);
      }
    },
    
    toggleEnabled: async (type: 'sources' | 'sinks' | 'routes', name: string, enable: boolean) => {
      try {
        
        // Directly call the appropriate API based on current state
        if (type === 'sources') {
          if (!enable) {
            await ApiService.disableSource(name);
          } else {
            await ApiService.enableSource(name);
          }
        } else if (type === 'sinks') {
          if (!enable) {
            await ApiService.disableSink(name);
          } else {
            await ApiService.enableSink(name);
          }
        } else if (type === 'routes') {
          if (!enable) {
            await ApiService.disableRoute(name);
          } else {
            await ApiService.enableRoute(name);
          }
        }
      } catch (error) {
        console.error(`Error toggling ${type} enabled status:`, error);
        setError(`Error toggling ${type} enabled status`);
      }
    },
    
    updateVolume: async (type: 'sources' | 'sinks' | 'routes', name: string, value: number) => {
      try {
        if (type === 'sources') {
          await ApiService.updateSourceVolume(name, value);
        } else if (type === 'sinks') {
          await ApiService.updateSinkVolume(name, value);
        } else if (type === 'routes') {
          await ApiService.updateRouteVolume(name, value);
        }
        
        // Add to recents when volume is changed
        addToRecents(type, name);
      } catch (error) {
        console.error(`Error updating ${type} volume:`, error);
        setError(`Error updating ${type} volume`);
      }
    },
    
    updateTimeshift: async (type: 'sources' | 'sinks' | 'routes', name: string, value: number) => {
      try {
        switch (type) {
          case 'sources':
            await ApiService.updateSourceTimeshift(name, value);
            break;
          case 'sinks':
            await ApiService.updateSinkTimeshift(name, value);
            break;
          case 'routes':
            await ApiService.updateRouteTimeshift(name, value);
            break;
        }
      } catch (error) {
        console.error(`Error updating ${type} timeshift:`, error);
        setError(`Error updating ${type} timeshift`);
      }
    },
    
    showEqualizer: (show: boolean, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: Source | Sink | Route) => {
      if (show && item) {
        openInNewWindow(`/site/equalizer?type=${type}&name=${encodeURIComponent(item.name)}`);
      }
    },
    
    showVNC: (show: boolean, source: Source) => {
      if (show && source && source.vnc_ip && source.vnc_port) {
        openInNewWindow(`/site/vnc?ip=${source.vnc_ip}&port=${source.vnc_port}`);
      }
    },
    
    toggleActiveSource: (name: string) => {
      onToggleActiveSource(name);
    },

    transcribeSink: (ip: string) => {
      onTranscribeSink(ip);
    },
    
    listenToSink: (name: string | null) => {
      onListenToSink(name);
    },
    
    visualizeSink: (name: string | null) => {
      onVisualizeSink(name);
    },
    
    controlSource: async (name: string, action: 'play' | 'pause' | 'prevtrack' | 'nexttrack') => {
      try {
        // The API only supports 'play', 'prevtrack', and 'nexttrack'
        // For simplicity, we'll just map 'pause' to 'play' since the API likely toggles play/pause
        const apiAction = action === 'pause' ? 'play' : action as 'play' | 'prevtrack' | 'nexttrack';
        await ApiService.controlSource(name, apiAction);
      } catch (error) {
        console.error('Error controlling source:', error);
        setError('Error controlling source');
      }
    },
    
    navigate: (type: 'sources' | 'sinks' | 'routes', name: string) => {
      navigateToItem(type, name);
    },
    
    // Placeholder functions that will be overridden in DesktopMenu.tsx
    confirmDelete: (type: 'sources' | 'sinks' | 'routes', name: string) => {
      console.warn('confirmDelete not implemented');
    },
    
    showSpeakerLayoutPage: (type: 'sources' | 'sinks' | 'routes', item: Source | Sink | Route) => {
      console.warn('showSpeakerLayoutPage not implemented');
    }
  };
};
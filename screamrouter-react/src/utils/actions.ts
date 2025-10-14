/**
 * React utility module for defining and creating actions used across the application.
 * These actions handle various operations such as toggling enabled status, deleting items,
 * updating volume and timeshift, managing starred items, editing items, showing modals,
 * controlling sources, and navigating to specific items.
 */

import ApiService from '../api/api';
import { Source, Sink, Route } from '../api/api';
import { openEditPage } from '../components/fullMenu/utils';

type ItemType = 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source';

/**
 * Interface defining the structure of actions available in the application.
 */
export interface Actions {
  /**
   * Toggles the enabled status of a source, sink, or route.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to toggle.
   */
  toggleEnabled: (type: ItemType, name: string) => Promise<void>;

  /**
   * Deletes a source, sink, or route.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to delete.
   */
  deleteItem: (type: ItemType, name: string) => Promise<void>;

  /**
   * Updates the volume of a source, sink, or route.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to update.
   * @param {number} volume - The new volume level.
   */
  updateVolume: (type: ItemType, name: string, volume: number) => Promise<void>;

  /**
   * Updates the timeshift of a source, sink, or route.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to update.
   * @param {number} timeshift - The new timeshift value.
   */
  updateTimeshift: (type: ItemType, name: string, timeshift: number) => Promise<void>;

  /**
   * Toggles an item as starred or unstarred.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to toggle.
   */
  toggleStar: (type: ItemType, name: string) => void;

  /**
   * Edits a source, sink, or route by opening an edit modal.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {Source | Sink | Route} item - The item to edit.
   */
  editItem: (type: ItemType, item: Source | Sink | Route) => void;

  /**
   * Shows or hides the equalizer modal for a source or sink.
   *
   * @param {boolean} show - Whether to show or hide the modal.
   * @param {ItemType} type - The type of item ('sources', 'sinks').
   * @param {Source | Sink | Route} item - The item associated with the equalizer.
   */
  showEqualizer: (show: boolean, type: ItemType, item: Source | Sink | Route) => void;

  /**
   * Shows or hides the VNC modal for a source.
   *
   * @param {boolean} show - Whether to show or hide the modal.
   * @param {Source} source - The source associated with the VNC.
   */
  showVNC: (show: boolean, source: Source) => void;

  /**
   * Controls playback actions for a source.
   *
   * @param {string} sourceName - The name of the source to control.
   * @param {'prevtrack' | 'play' | 'nexttrack'} action - The playback action ('prevtrack', 'play', 'nexttrack').
   */
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;

  /**
   * Toggles the Primary Source.
   *
   * @param {string} name - The name of the source to toggle as active.
   */
  toggleActiveSource: (name: string) => void;

  /**
   * Sets the sink to listen to.
   *
   * @param {Sink | null} sink - The sink to listen to, or null to stop listening.
   */
  listenToSink: (sink: Sink | null) => void;

  /**
   * Sets the sink to visualize.
   *
   * @param {Sink | null} sink - The sink to visualize, or null to stop visualizing.
   */
  visualizeSink: (sink: Sink | null) => void;

  /**
   * Navigates to a specific item by type and name.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to navigate to.
   */
  navigateToItem: (type: ItemType, name: string) => void;

  /**
   * Opens the channel mapping dialog for a source, sink, or route.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {Source | Sink | Route} item - The item to open channel mapping for.
   */
  openChannelMapping: (type: ItemType, item: Source | Sink | Route) => void;

  /**
   * Navigate function for ResourceCard components.
   *
   * @param {ItemType} type - The type of item ('sources', 'sinks', 'routes').
   * @param {string} name - The name of the item to navigate to.
   */
  navigate: (type: ItemType, name: string) => void;
}

/**
 * Factory function for creating actions with specific callbacks and state setters.
 *
 * @param {() => Promise<void>} fetchData - Function to fetch data from the API.
 * @param {(error: string | null) => void} setError - Function to set an error message.
 * @param {(type: ItemType, setter: (prevItems: string[]) => string[]) => void} setStarredItems - Function to update starred items.
 * @param {(show: boolean, type: ItemType, item: Source | Sink | Route) => void} setShowEqualizerModal - Function to show or hide the equalizer modal.
 * @param {(item: Source | Sink | Route | null) => void} setSelectedItem - Function to set the selected item.
 * @param {(type: ItemType | null) => void} setSelectedItemType - Function to set the type of the selected item.
 * @param {(show: boolean, source: Source) => void} setShowVNCModal - Function to show or hide the VNC modal.
 * @param {(setter: (prevActiveSource: string | null) => string | null) => void} setActiveSource - Function to set the Primary Source.
 * @param {(sink: Sink | null) => void} onListenToSink - Callback for listening to a sink.
 * @param {(sink: Sink | null) => void} onVisualizeSink - Callback for visualizing a sink.
 * @param {(show: boolean) => void} setShowEditModal - Function to show or hide the edit modal.
 * @param {(type: ItemType, name: string) => void} onNavigateToItem - Callback for navigating to an item.
 * @returns {Actions} An object containing all defined actions.
 */
export const createActions = (
  fetchData: () => Promise<void>,
  setError: (error: string | null) => void,
  setStarredItems: (type: ItemType, setter: (prevItems: string[]) => string[]) => void,
  setShowEqualizerModal: (show: boolean, type: ItemType, item: Source | Sink | Route) => void,
  setSelectedItem: (item: Source | Sink | Route | null) => void,
  setSelectedItemType: (type: ItemType | null) => void,
  setShowVNCModal: (show: boolean, source: Source) => void,
  setActiveSource: (setter: (prevActiveSource: string | null) => string | null) => void,
  onListenToSink: (sink: Sink | null) => void,
  onVisualizeSink: (sink: Sink | null) => void,
  setShowEditModal: (show: boolean) => void,
  onNavigateToItem: (type: ItemType, name: string) => void
): Actions => ({
  toggleEnabled: async (type, name) => {
    try {
      const item = type === 'sources' ? await ApiService.getSources()
                 : type === 'sinks' ? await ApiService.getSinks()
                 : await ApiService.getRoutes();
      const targetItem = Object.values(item.data).find((i: Source | Sink | Route) => i.name === name);
      if (!targetItem) throw new Error(`${type} not found`);

      const updateMethod = type === 'sources' ? ApiService.updateSource
                         : type === 'sinks' ? ApiService.updateSink
                         : ApiService.updateRoute;
      await updateMethod(name, { enabled: !targetItem.enabled });
      fetchData();
    } catch (error) {
      console.error(`Error toggling ${type} status:`, error);
      setError(`Failed to update ${type} status. Please try again.`);
    }
  },
  deleteItem: async (type, name) => {
    try {
      const deleteMethod = type === 'sources' ? ApiService.deleteSource
                         : type === 'sinks' ? ApiService.deleteSink
                         : ApiService.deleteRoute;
      await deleteMethod(name);
      fetchData();
    } catch (error) {
      console.error(`Error deleting ${type}:`, error);
      setError(`Failed to delete ${type}. Please try again.`);
    }
  },
  updateVolume: async (type, name, volume) => {
    try {
      const updateMethod = type === 'sources' ? ApiService.updateSourceVolume
                         : type === 'sinks' ? ApiService.updateSinkVolume
                         : ApiService.updateRouteVolume;
      await updateMethod(name, volume);
      fetchData();
    } catch (error) {
      console.error(`Error updating ${type} volume:`, error);
      setError(`Failed to update ${type} volume. Please try again.`);
    }
  },
  updateTimeshift: async (type, name, timeshift) => {
    try {
      const updateMethod = type === 'sources' ? ApiService.updateSourceTimeshift
                         : type === 'sinks' ? ApiService.updateSinkTimeshift
                         : ApiService.updateRouteTimeshift;
      await updateMethod(name, timeshift);
      fetchData();
    } catch (error) {
      console.error(`Error updating ${type} timeshift:`, error);
      setError(`Failed to update ${type} timeshift. Please try again.`);
    }
  },
  toggleStar: (type, name) => {
    setStarredItems(type, (prevItems: string[]) => {
      const newItems = prevItems.includes(name)
        ? prevItems.filter(item => item !== name)
        : [...prevItems, name];
      localStorage.setItem(`starred${type.charAt(0).toUpperCase() + type.slice(1)}`, JSON.stringify(newItems));
      return newItems;
    });
  },
  editItem: (type, item) => {
    // Use the new openEditPage function instead of showing a modal
    openEditPage(type, item);
  },
  showEqualizer: (show, type, item) => {
    setSelectedItem(item);
    setSelectedItemType(type);
    setShowEqualizerModal(show, type, item);
  },
  showVNC: (show, source) => {
    setSelectedItem(source);
    setShowVNCModal(show, source);
  },
  controlSource: async (sourceName, action) => {
    try {
      await ApiService.controlSource(sourceName, action);
      fetchData();
    } catch (error) {
      console.error('Error controlling source:', error);
      setError('Failed to control source. Please try again.');
    }
  },
  toggleActiveSource: (name) => {
    setActiveSource((prevActiveSource: string | null) => prevActiveSource === name ? null : name);
  },
  listenToSink: onListenToSink,
  visualizeSink: onVisualizeSink,
  navigateToItem: onNavigateToItem,
  openChannelMapping: (type, item) => {
    // Open speaker layout standalone page for the item
    window.open(`/site/speaker-layout-standalone?type=${type}&name=${encodeURIComponent(item.name)}`, '_blank');
  },
  navigate: onNavigateToItem,
});

export default createActions;

import { Type } from 'typescript';
import ApiService from '../api/api';
import { Source, Sink, Route } from '../api/api';

type ItemType = 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source';

export interface Actions {
  toggleEnabled: (type: ItemType, name: string) => Promise<void>;
  deleteItem: (type: ItemType, name: string) => Promise<void>;
  updateVolume: (type: ItemType, name: string, volume: number) => Promise<void>;
  updateTimeshift: (type: ItemType, name: string, timeshift: number) => Promise<void>;
  toggleStar: (type: ItemType, name: string) => void;
  editItem: (type: ItemType, item: Source | Sink | Route) => void;
  showEqualizer: (show: boolean, type: ItemType, item: Source | Sink | Route, source: Source) => void;
  showVNC: (show: boolean, source: Source) => void;
  controlSource: (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => Promise<void>;
  toggleActiveSource: (name: string) => void;
  listenToSink: (sink: Sink | null) => void;
  visualizeSink: (sink: Sink | null) => void;
}

export const createActions = (
  fetchData: () => Promise<void>,
  setError: (error: string | null) => void,
  setStarredItems: (type: ItemType, setter: (prevItems: string[]) => string[]) => void,
  setShowEqualizerModal: (show: boolean, type: 'sources' | 'sinks' | 'routes' | 'group-sink' | 'group-source', item: Source | Sink | Route) => void,
  setSelectedItem: (item: any) => void,
  setSelectedItemType: (type: ItemType | null) => void,
  setShowVNCModal: (show: boolean, source: Source) => void,
  setActiveSource: (setter: (prevActiveSource: string | null) => string | null) => void,
  onListenToSink: (sink: Sink | null) => void,
  onVisualizeSink: (sink: Sink | null) => void,
  setShowEditModal: (show: boolean) => void
): Actions => ({
  toggleEnabled: async (type, name) => {
    try {
      const item = type === 'sources' ? await ApiService.getSources()
                 : type === 'sinks' ? await ApiService.getSinks()
                 : await ApiService.getRoutes();
      const targetItem = item.data.find((i: any) => i.name === name);
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
    setSelectedItem(item);
    setSelectedItemType(type);
    setShowEditModal(true);
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
});

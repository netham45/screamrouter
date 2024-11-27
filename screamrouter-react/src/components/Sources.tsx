/**
 * React component for displaying and managing a list of sources.
 * It includes functionalities such as sorting, adding, deleting, and toggling sources,
 * as well as handling actions like enabling/disabling, updating volume/timeshift,
 * controlling sources, and showing modals for equalizer and VNC.
 */
import React, { useState, useEffect } from 'react';
import SourceList from './SourceList';
import { SortConfig, useAnchorFlash } from '../utils/commonUtils';
import { useAppContext } from '../context/AppContext';
import { Actions } from '../utils/actions';
import ActionButton from './controls/ActionButton';
import ConfirmationModal from './ConfirmationModal';
import ApiService from '../api/api';

/**
 * React functional component for rendering the Sources page.
 *
 * @returns {JSX.Element} The rendered JSX element.
 */
const Sources: React.FC = () => {
  /**
   * Custom hook to handle anchor flash effects.
   */
  useAnchorFlash();

  /**
   * Destructured state and actions from the AppContext.
   */
  const { 
    sources, 
    routes, 
    activeSource, 
    fetchSources, 
    toggleEnabled, 
    updateVolume, 
    updateTimeshift,
    controlSource,
    openVNCModal,
    openEqualizerModal,
    onToggleActiveSource,
    setSelectedItem,
    setSelectedItemType,
    setShowEditModal
  } = useAppContext();
  
  /**
   * State variable to manage the current sort configuration.
   */
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: '', direction: 'asc' });
  /**
   * State variable to indicate if sources are currently being loaded.
   */
  const [isLoading, setIsLoading] = useState(false);
  /**
   * State variable to store any error messages related to source operations.
   */
  const [error, setError] = useState<string | null>(null);
  /**
   * State variable to manage the list of starred sources.
   */
  const [starredSources, setStarredSources] = useState<string[]>([]);
  /**
   * State variable to indicate if the delete confirmation modal is shown.
   */
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  /**
   * State variable to store the name of the source to be deleted.
   */
  const [sourceToDelete, setSourceToDelete] = useState<string | null>(null);

  /**
   * Effect hook to load sources if they are not already loaded and to restore starred sources from local storage.
   */
  useEffect(() => {
    if (sources.length === 0) {
      loadSources();
    }
    const savedStarredSources = localStorage.getItem('starredSources');
    if (savedStarredSources) {
      setStarredSources(JSON.parse(savedStarredSources));
    }
  }, []);

  /**
   * Function to asynchronously fetch sources from the API.
   */
  const loadSources = async () => {
    setIsLoading(true);
    setError(null);
    try {
      await fetchSources();
    } catch (err) {
      console.error('Error fetching sources:', err);
      setError('Failed to fetch sources. Please try again.');
    } finally {
      setIsLoading(false);
    }
  };

  /**
   * Function to handle sorting of sources based on a given key.
   *
   * @param {string} key - The key to sort by (e.g., 'name', 'ip').
   */
  const onSort = (key: string) => {
    setSortConfig(prevConfig => ({
      key,
      direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
    }));
  };

  /**
   * Function to toggle the star status of a source.
   *
   * @param {string} name - The name of the source.
   */
  const onToggleStar = async (name: string) => {
    setStarredSources(prev => {
      const newStarred = prev.includes(name)
        ? prev.filter(source => source !== name)
        : [...prev, name];
      localStorage.setItem('starredSources', JSON.stringify(newStarred));
      return newStarred;
    });
  };

  /**
   * Function to handle adding a new source.
   */
  const handleAddSource = () => {
    setSelectedItem(null);
    setSelectedItemType('sources');
    setShowEditModal(true);
  };

  /**
   * Function to handle adding a new group of sources.
   */
  const handleAddGroup = () => {
    setSelectedItem(null);
    setSelectedItemType('group-source');
    setShowEditModal(true);
  };

  /**
   * Function to handle deleting the selected source.
   */
  const handleDeleteSource = async () => {
    if (sourceToDelete) {
      try {
        await ApiService.deleteSource(sourceToDelete);
        console.log(`Source deleted: ${sourceToDelete}`);
        await fetchSources();
      } catch (error) {
        console.error('Error deleting source:', error);
        setError('Failed to delete source. Please try again.');
      }
    }
    setShowDeleteModal(false);
    setSourceToDelete(null);
  };

  /**
   * Actions object containing functions to manage sources.
   */
  const actions: Actions = {
    toggleEnabled: async (type, name) => {
      if (type === 'sources') {
        const source = sources.find(s => s.name === name);
        if (source) {
          await toggleEnabled('sources', name, source.enabled);
        }
      }
    },
    updateVolume: (type, name, volume) => updateVolume(type, name, volume),
    updateTimeshift: (type, name, timeshift) => updateTimeshift(type, name, timeshift),
    controlSource,
    toggleStar: async (type, name) => {
      if (type === 'sources') await onToggleStar(name);
    },
    deleteItem: async (type, name) => {
      if (type === 'sources') {
        setSourceToDelete(name);
        setShowDeleteModal(true);
      }
    },
    editItem: (type, item) => {
      if (type === 'sources' && 'ip' in item) {
        setSelectedItem(item);
        setSelectedItemType('sources');
        setShowEditModal(true);
      }
    },
    showEqualizer: (type, item) => {
      if (type === 'sources' && 'ip' in item) {
        openEqualizerModal(item, type);
      }
    },
    showVNC: (source) => {
      if ('ip' in source) {
        openVNCModal(source);
      }
    },
    listenToSink: () => {}, // Not applicable for sources
    visualizeSink: () => {}, // Not applicable for sources
    toggleActiveSource: (name: string) => onToggleActiveSource(name)
  };

  /**
   * Render loading message if sources are being fetched.
   */
  if (isLoading) {
    return <div>Loading sources...</div>;
  }

  /**
   * Render error message if there is an issue fetching sources.
   */
  if (error) {
    return <div>Error: {error}</div>;
  }

  /**
   * Render the Sources page with action buttons and source list.
   */
  return (
    <div className="sources-container">
      <h2>Sources</h2>
      <div className="action-buttons">
        <ActionButton onClick={handleAddSource}>Add Source</ActionButton>
        <ActionButton onClick={handleAddGroup}>Add Group</ActionButton>
      </div>
      {sources.length === 0 ? (
        <div>No sources available</div>
      ) : (
        <SourceList
          sources={sources}
          routes={routes}
          starredSources={starredSources}
          activeSource={activeSource}
          actions={actions}
          sortConfig={sortConfig}
          onSort={onSort}
        />
      )}
      <ConfirmationModal
        isOpen={showDeleteModal}
        onClose={() => setShowDeleteModal(false)}
        onConfirm={handleDeleteSource}
        message={`Are you sure you want to delete the source "${sourceToDelete}"?`}
      />
    </div>
  );
};

export default Sources;

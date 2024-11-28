/**
 * React component for displaying a list of sinks with functionalities such as adding, deleting,
 * starring, enabling/disabling, adjusting volume and timeshift, and performing actions like listening to or visualizing the sink.
 */
import React, { useState, useEffect } from 'react';
import SinkList from './SinkList';
import { useAppContext } from '../context/AppContext';
import { Actions } from '../utils/actions';
import ActionButton from './controls/ActionButton';
import { SortConfig, useAnchorFlash } from '../utils/commonUtils';
import ConfirmationModal from './ConfirmationModal';
import ApiService from '../api/api';
import { Sink } from '../api/api';

/**
 * React functional component for rendering the sinks section.
 *
 * @returns {JSX.Element} The rendered JSX element.
 */
const Sinks: React.FC = () => {
  // Hook to flash anchor elements
  useAnchorFlash();

  // Destructuring context values
  const { 
    sinks, 
    routes, 
    fetchSinks, 
    toggleEnabled, 
    updateVolume, 
    updateTimeshift, 
    openEqualizerModal,
    onListenToSink,
    onVisualizeSink,
    listeningToSink,
    visualizingSink,
    setSelectedItem,
    setSelectedItemType,
    setShowEditModal
  } = useAppContext();
  
  /**
   * State variable to manage loading state.
   */
  const [isLoading, setIsLoading] = useState(false);
  /**
   * State variable to manage error messages.
   */
  const [error, setError] = useState<string | null>(null);
  /**
   * State variable to manage starred sinks.
   */
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  /**
   * State variable to manage sorting configuration.
   */
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: 'name', direction: 'asc' });
  /**
   * State variable to manage visibility of the delete confirmation modal.
   */
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  /**
   * State variable to manage the sink selected for deletion.
   */
  const [sinkToDelete, setSinkToDelete] = useState<string | null>(null);

  /**
   * Effect hook to load sinks and restore starred sinks from local storage on component mount.
   */
  useEffect(() => {
    if (sinks.length === 0) {
      loadSinks();
    }
    const savedStarredSinks = localStorage.getItem('starredSinks');
    if (savedStarredSinks) {
      setStarredSinks(JSON.parse(savedStarredSinks));
    }
  }, []);

  /**
   * Function to fetch sinks from the API.
   */
  const loadSinks = async () => {
    setIsLoading(true);
    setError(null);
    try {
      await fetchSinks();
    } catch (err) {
      console.error('Error fetching sinks:', err);
      setError('Failed to fetch sinks. Please try again.');
    } finally {
      setIsLoading(false);
    }
  };

  /**
   * Function to handle sorting of sinks.
   *
   * @param {string} key - The key by which sinks are sorted.
   */
  const onSort = (key: string) => {
    setSortConfig(prevConfig => ({
      key,
      direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
    }));
  };

  /**
   * Function to toggle the star status of a sink.
   *
   * @param {string} name - The name of the sink.
   */
  const onToggleStar = async (name: string) => {
    setStarredSinks(prev => {
      const newStarred = prev.includes(name)
        ? prev.filter(sink => sink !== name)
        : [...prev, name];
      localStorage.setItem('starredSinks', JSON.stringify(newStarred));
      return newStarred;
    });
  };

  /**
   * Function to handle adding a new sink.
   */
  const handleAddSink = () => {
    setSelectedItem(null);
    setSelectedItemType('sinks');
    setShowEditModal(true);
  };

  /**
   * Function to handle adding a new group sink.
   */
  const handleAddGroup = () => {
    setSelectedItem(null);
    setSelectedItemType('group-sink');
    setShowEditModal(true);
  };

  /**
   * Function to handle deleting a selected sink.
   */
  const handleDeleteSink = async () => {
    if (sinkToDelete) {
      try {
        await ApiService.deleteSink(sinkToDelete);
        console.log(`Sink deleted: ${sinkToDelete}`);
        await fetchSinks();
      } catch (error) {
        console.error('Error deleting sink:', error);
        setError('Failed to delete sink. Please try again.');
      }
    }
    setShowDeleteModal(false);
    setSinkToDelete(null);
  };

  /**
   * Actions object containing functions to manage sinks.
   */
  const actions: Actions = {
    toggleEnabled: async (type, name) => {
      if (type === 'sinks') {
        const sink = sinks.find(s => s.name === name);
        if (sink) {
          await toggleEnabled('sinks', name, sink.enabled);
        }
      }
    },
    updateVolume: (type, name, volume) => updateVolume(type, name, volume),
    updateTimeshift: (type, name, timeshift) => updateTimeshift(type, name, timeshift),
    controlSource: async () => {}, // Not applicable for sinks
    toggleStar: async (type, name) => {
      if (type === 'sinks') await onToggleStar(name);
    },
    deleteItem: async (type, name) => {
      if (type === 'sinks') {
        setSinkToDelete(name);
        setShowDeleteModal(true);
      }
    },
    editItem: (type, item) => {
      if (type === 'sinks' && typeof item === 'object' && 'ip' in item) {
        setSelectedItem(item as Sink);
        setSelectedItemType('sinks');
        setShowEditModal(true);
      }
    },
    showEqualizer: (show, type, item) => {
      if (type === 'sinks' && typeof item === 'object' && 'ip' in item) {
        openEqualizerModal(item as Sink, type);
      }
    },
    showVNC: () => {}, // Not applicable for sinks
    listenToSink: onListenToSink,
    visualizeSink: onVisualizeSink,
    toggleActiveSource: async () => {}, // Not applicable for sinks
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    navigateToItem: (_type, _name) => {} // Placeholder function for navigateToItem
  };

  /**
   * Render loading state.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  if (isLoading) {
    return <div>Loading sinks...</div>;
  }

  /**
   * Render error state.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  if (error) {
    return <div>Error: {error}</div>;
  }

  /**
   * Main render function for the Sinks component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  return (
    <div className="sinks-container">
      <h2>Sinks</h2>
      <div className="action-buttons">
        <ActionButton onClick={handleAddSink}>Add Sink</ActionButton>
        <ActionButton onClick={handleAddGroup}>Add Group</ActionButton>
      </div>
      {sinks.length === 0 ? (
        <div>No sinks available</div>
      ) : (
        <SinkList
          sinks={sinks}
          routes={routes}
          starredSinks={starredSinks}
          actions={actions}
          listeningToSink={listeningToSink?.name}
          visualizingSink={visualizingSink?.name}
          sortConfig={sortConfig}
          onSort={onSort}
        />
      )}
      <ConfirmationModal
        isOpen={showDeleteModal}
        onClose={() => setShowDeleteModal(false)}
        onConfirm={handleDeleteSink}
        message={`Are you sure you want to delete the sink "${sinkToDelete}"?`}
      />
    </div>
  );
};

export default Sinks;

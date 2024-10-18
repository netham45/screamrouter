import React, { useState, useEffect } from 'react';
import SinkList from './SinkList';
import { useAppContext } from '../context/AppContext';
import { Actions } from '../utils/actions';
import ActionButton from './controls/ActionButton';
import { SortConfig, useAnchorFlash } from '../utils/commonUtils';
import ConfirmationModal from './ConfirmationModal';
import ApiService from '../api/api';

const Sinks: React.FC = () => {
  useAnchorFlash();

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
  
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: 'name', direction: 'asc' });
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [sinkToDelete, setSinkToDelete] = useState<string | null>(null);

  useEffect(() => {
    if (sinks.length === 0) {
      loadSinks();
    }
    const savedStarredSinks = localStorage.getItem('starredSinks');
    if (savedStarredSinks) {
      setStarredSinks(JSON.parse(savedStarredSinks));
    }
  }, []);

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

  const onSort = (key: string) => {
    setSortConfig(prevConfig => ({
      key,
      direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
    }));
  };

  const onToggleStar = async (name: string) => {
    setStarredSinks(prev => {
      const newStarred = prev.includes(name)
        ? prev.filter(sink => sink !== name)
        : [...prev, name];
      localStorage.setItem('starredSinks', JSON.stringify(newStarred));
      return newStarred;
    });
  };

  const handleAddSink = () => {
    setSelectedItem(null);
    setSelectedItemType('sinks');
    setShowEditModal(true);
  };

  const handleAddGroup = () => {
    setSelectedItem(null);
    setSelectedItemType('group-sink');
    setShowEditModal(true);
  };

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
      if (type === 'sinks' && 'ip' in item) {
        setSelectedItem(item);
        setSelectedItemType('sinks');
        setShowEditModal(true);
      }
    },
    showEqualizer: (type, item) => {
      if (type === 'sinks' && 'ip' in item) {
        openEqualizerModal(item, type);
      }
    },
    showVNC: () => {}, // Not applicable for sinks
    listenToSink: onListenToSink,
    visualizeSink: onVisualizeSink,
    toggleActiveSource: () => {} // Not applicable for sinks
  };

  if (isLoading) {
    return <div>Loading sinks...</div>;
  }

  if (error) {
    return <div>Error: {error}</div>;
  }

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
          listeningToSink={listeningToSink}
          visualizingSink={visualizingSink}
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

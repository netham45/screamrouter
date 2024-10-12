import React, { useState, useEffect } from 'react';
import { Source, Route } from '../api/api';
import SourceList from './SourceList';
import { SortConfig, useAnchorFlash } from '../utils/commonUtils';
import { useAppContext } from '../context/AppContext';
import { Actions } from '../utils/actions';
import ActionButton from './controls/ActionButton';
import ConfirmationModal from './ConfirmationModal';
import ApiService from '../api/api';

const Sources: React.FC = () => {
  useAnchorFlash();

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
  
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: '', direction: 'asc' });
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [starredSources, setStarredSources] = useState<string[]>([]);
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [sourceToDelete, setSourceToDelete] = useState<string | null>(null);

  useEffect(() => {
    if (sources.length === 0) {
      loadSources();
    }
    const savedStarredSources = localStorage.getItem('starredSources');
    if (savedStarredSources) {
      setStarredSources(JSON.parse(savedStarredSources));
    }
  }, []);

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

  const onSort = (key: string) => {
    setSortConfig(prevConfig => ({
      key,
      direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
    }));
  };

  const onToggleStar = async (name: string) => {
    setStarredSources(prev => {
      const newStarred = prev.includes(name)
        ? prev.filter(source => source !== name)
        : [...prev, name];
      localStorage.setItem('starredSources', JSON.stringify(newStarred));
      return newStarred;
    });
  };

  const handleAddSource = () => {
    setSelectedItem(null);
    setSelectedItemType('sources');
    setShowEditModal(true);
  };

  const handleAddGroup = () => {
    setSelectedItem(null);
    setSelectedItemType('group-source');
    setShowEditModal(true);
  };

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

  if (isLoading) {
    return <div>Loading sources...</div>;
  }

  if (error) {
    return <div>Error: {error}</div>;
  }

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

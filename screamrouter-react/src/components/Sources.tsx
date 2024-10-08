import React, { useState, useEffect, useRef, useCallback } from 'react';
import { useLocation } from 'react-router-dom';
import { Source, Route } from '../api/api';
import Equalizer from './Equalizer';
import AddEditSource from './AddEditSource';
import AddEditGroup from './AddEditGroup';
import VNC from './VNC';
import SourceList from './SourceList';
import { useAppContext } from '../context/AppContext';
import ApiService from '../api/api';

const Sources: React.FC = () => {
  const {
    sources,
    routes,
    activeSource,
    toggleEnabled,
    updateVolume,
    controlSource,
    onToggleActiveSource,
    fetchSources,
    fetchRoutes
  } = useAppContext();

  const [showAddModal, setShowAddModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showGroupModal, setShowGroupModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [showVNCModal, setShowVNCModal] = useState(false);
  const [selectedSource, setSelectedSource] = useState<Source | undefined>(undefined);
  const [error, setError] = useState<string | null>(null);
  const [starredSources, setStarredSources] = useState<string[]>([]);
  const [expandedRoutes, setExpandedRoutes] = useState<string[]>([]);
  const [sortedSources, setSortedSources] = useState<Source[]>([]);

  const sourceRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const dragItem = useRef<number | null>(null);
  const dragOverItem = useRef<number | null>(null);

  const location = useLocation();

  useEffect(() => {
    fetchSources();
    fetchRoutes();
    const starred = JSON.parse(localStorage.getItem('starredSources') || '[]');
    setStarredSources(starred);
  }, [fetchSources, fetchRoutes]);

  useEffect(() => {
    const hash = location.hash;
    if (hash) {
      const sourceName = decodeURIComponent(hash.replace('#source-', ''));
      jumpToAnchor(sourceName);
    }
  }, [location.hash, sources]);

  const deleteSource = async (name: string) => {
    if (window.confirm(`Are you sure you want to delete the source "${name}"?`)) {
      try {
        await ApiService.deleteSource(name);
        fetchSources();
      } catch (error) {
        console.error('Error deleting source:', error);
        setError('Failed to delete source. Please try again.');
      }
    }
  };

  const toggleStar = (name: string) => {
    const newStarredSources = starredSources.includes(name)
      ? starredSources.filter(source => source !== name)
      : [...starredSources, name];
    setStarredSources(newStarredSources);
    localStorage.setItem('starredSources', JSON.stringify(newStarredSources));
    jumpToAnchor(name);
  };

  const sortSources = useCallback((sourcesToSort: Source[]) => {
    return [...sourcesToSort].sort((a, b) => {
      const aStarred = starredSources.includes(a.name);
      const bStarred = starredSources.includes(b.name);
      if (aStarred !== bStarred) return aStarred ? -1 : 1;
      if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
      return 0;
    });
  }, [starredSources]);

  useEffect(() => {
    setSortedSources(sortSources(sources));
  }, [sources, starredSources, sortSources]);

  const jumpToAnchor = useCallback((name: string) => {
    const element = sourceRefs.current[name];
    if (element) {
      element.scrollIntoView({ behavior: 'smooth', block: 'center' });
      element.classList.remove('flash');
      void element.offsetWidth; // Trigger reflow
      element.classList.add('flash');
    }
  }, []);

  const onDragStart = (e: React.DragEvent<HTMLSpanElement>, index: number) => {
    dragItem.current = index;
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/html', e.currentTarget.parentElement!.outerHTML);
    e.currentTarget.parentElement!.style.opacity = '0.5';
  };

  const onDragEnter = (e: React.DragEvent<HTMLTableRowElement>, index: number) => {
    dragOverItem.current = index;
    e.currentTarget.classList.add('drag-over');
  };

  const onDragLeave = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.currentTarget.classList.remove('drag-over');
  };

  const onDragOver = (e: React.DragEvent<HTMLTableRowElement>) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
  };

  const onDragEnd = (e: React.DragEvent<HTMLSpanElement>) => {
    e.currentTarget.parentElement!.style.opacity = '1';
    e.currentTarget.parentElement!.classList.remove('drag-over');
  };

  const onDrop = async (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    const draggedIndex = dragItem.current;

    if (draggedIndex === null || draggedIndex === targetIndex) {
      return;
    }

    const newSources = [...sources];
    const [reorderedItem] = newSources.splice(draggedIndex, 1);
    newSources.splice(targetIndex, 0, reorderedItem);

    setSortedSources(sortSources(newSources));

    try {
      await ApiService.reorderSource(reorderedItem.name, targetIndex);
      fetchSources();
      jumpToAnchor(reorderedItem.name);
    } catch (error) {
      console.error('Error reordering source:', error);
      setError('Failed to reorder source. Please try again.');
    }

    dragItem.current = null;
    dragOverItem.current = null;
  };

  const getActiveRoutes = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName && route.enabled);
  };

  const getDisabledRoutes = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName && !route.enabled);
  };

  const toggleExpandRoutes = (name: string) => {
    setExpandedRoutes(prev =>
      prev.includes(name) ? prev.filter(n => n !== name) : [...prev, name]
    );
  };

  const onToggleSource = useCallback((name: string) => {
    const sourceToToggle = sources.find(s => s.name === name);
    if (sourceToToggle) {
      const newEnabledState = !sourceToToggle.enabled;
      
      // Update local state immediately
      setSortedSources(prevSources => 
        prevSources.map(s => 
          s.name === name ? { ...s, enabled: newEnabledState } : s
        )
      );

      // Call the API to update the backend
      toggleEnabled('sources', name, !newEnabledState);

      // If we're disabling the active source, update active source state
      if (activeSource === name && !newEnabledState) {
        onToggleActiveSource(name);
      }
    }
  }, [sources, toggleEnabled, activeSource, onToggleActiveSource]);

  return (
    <div className="sources">
      <h2>Sources</h2>
      {error && <div className="error-message">{error}</div>}
      <div className="actions">
        <button onClick={() => setShowAddModal(true)}>Add Source</button>
        <button onClick={() => setShowGroupModal(true)}>Add Group</button>
      </div>
      <SourceList
        sources={sortedSources}
        routes={routes}
        starredSources={starredSources}
        activeSource={activeSource}
        onToggleSource={onToggleSource}
        onDeleteSource={deleteSource}
        onUpdateVolume={(name, volume) => updateVolume('sources', name, volume)}
        onToggleStar={toggleStar}
        onToggleActiveSource={onToggleActiveSource}
        onEditSource={(source) => { setSelectedSource(source); setShowEditModal(true); }}
        onShowEqualizer={(source) => { setSelectedSource(source); setShowEqualizerModal(true); }}
        onShowVNC={(source) => { setSelectedSource(source); setShowVNCModal(true); }}
        onControlSource={controlSource}
        sourceRefs={sourceRefs}
        onDragStart={onDragStart}
        onDragEnter={onDragEnter}
        onDragLeave={onDragLeave}
        onDragOver={onDragOver}
        onDrop={onDrop}
        onDragEnd={onDragEnd}
        jumpToAnchor={jumpToAnchor}
        getActiveRoutes={getActiveRoutes}
        getDisabledRoutes={getDisabledRoutes}
        expandedRoutes={expandedRoutes}
        toggleExpandRoutes={toggleExpandRoutes}
      />

      {showAddModal && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowAddModal(false)}>×</button>
            <AddEditSource
              onClose={() => setShowAddModal(false)}
              onSubmit={() => { fetchSources(); setShowAddModal(false); }}
            />
          </div>
        </div>
      )}

      {showEditModal && selectedSource && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEditModal(false)}>×</button>
            <AddEditSource
              source={selectedSource}
              onClose={() => setShowEditModal(false)}
              onSubmit={() => { fetchSources(); setShowEditModal(false); }}
            />
          </div>
        </div>
      )}

      {showGroupModal && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowGroupModal(false)}>×</button>
            <AddEditGroup
              type="source"
              group={selectedSource}
              onClose={() => setShowGroupModal(false)}
              onSubmit={() => { fetchSources(); setShowGroupModal(false); }}
            />
          </div>
        </div>
      )}

      {showEqualizerModal && selectedSource && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEqualizerModal(false)}>×</button>
            <Equalizer
              item={selectedSource}
              type="sources"
              onClose={() => setShowEqualizerModal(false)}
              onDataChange={fetchSources}
            />
          </div>
        </div>
      )}

      {showVNCModal && selectedSource && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowVNCModal(false)}>×</button>
            <VNC
              source={selectedSource}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Sources;
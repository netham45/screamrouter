import React, { useState, useEffect, useRef, useCallback } from 'react';
import { useLocation } from 'react-router-dom';
import ApiService, { Sink, Route } from '../api/api';
import Equalizer from './Equalizer';
import AddEditSink from './AddEditSink';
import AddEditGroup from './AddEditGroup';
import SinkList from './SinkList';
import { useAppContext } from '../context/AppContext';

const Sinks: React.FC = () => {
  const { listeningToSink, visualizingSink, onListenToSink, onVisualizeSink } = useAppContext();
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [showAddModal, setShowAddModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showGroupModal, setShowGroupModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedSink, setSelectedSink] = useState<Sink | undefined>(undefined);
  const [error, setError] = useState<string | null>(null);
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  const [expandedRoutes, setExpandedRoutes] = useState<string[]>([]);
  const [sortedSinks, setSortedSinks] = useState<Sink[]>([]);

  const sinkRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const dragItem = useRef<number | null>(null);
  const dragOverItem = useRef<number | null>(null);

  const location = useLocation();

  const fetchSinks = async () => {
    try {
      const response = await ApiService.getSinks();
      setSinks(response.data);
      setError(null);
    } catch (error) {
      console.error('Error fetching sinks:', error);
      setError('Failed to fetch sinks. Please try again later.');
    }
  };

  const fetchRoutes = async () => {
    try {
      const response = await ApiService.getRoutes();
      setRoutes(response.data);
    } catch (error) {
      console.error('Error fetching routes:', error);
    }
  };

  useEffect(() => {
    fetchSinks();
    fetchRoutes();
    const starred = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    setStarredSinks(starred);
  }, []);

  useEffect(() => {
    const hash = location.hash;
    if (hash) {
      const sinkName = decodeURIComponent(hash.replace('#sink-', ''));
      jumpToAnchor(sinkName);
    }
  }, [location.hash, sinks]);

  const toggleSink = async (name: string) => {
    try {
      const sinkToUpdate = sinks.find(sink => sink.name === name);
      if (sinkToUpdate) {
        await ApiService[sinkToUpdate.enabled ? 'disableSink' : 'enableSink'](name);
        await fetchSinks();
        jumpToAnchor(name);
      }
    } catch (error) {
      console.error('Error updating sink:', error);
      setError('Failed to update sink. Please try again.');
    }
  };

  const deleteSink = async (name: string) => {
    if (window.confirm(`Are you sure you want to delete the sink "${name}"?`)) {
      try {
        await ApiService.deleteSink(name);
        await fetchSinks();
      } catch (error) {
        console.error('Error deleting sink:', error);
        setError('Failed to delete sink. Please try again.');
      }
    }
  };

  const updateVolume = async (name: string, volume: number) => {
    try {
      await ApiService.updateSinkVolume(name, volume);
      await fetchSinks();
    } catch (error) {
      console.error('Error updating sink volume:', error);
      setError('Failed to update volume. Please try again.');
    }
  };

  const toggleStar = (name: string) => {
    const newStarredSinks = starredSinks.includes(name)
      ? starredSinks.filter(sink => sink !== name)
      : [...starredSinks, name];
    setStarredSinks(newStarredSinks);
    localStorage.setItem('starredSinks', JSON.stringify(newStarredSinks));
    jumpToAnchor(name);
  };

  const sortSinks = useCallback((sinksToSort: Sink[]) => {
    return [...sinksToSort].sort((a, b) => {
      const aStarred = starredSinks.includes(a.name);
      const bStarred = starredSinks.includes(b.name);
      if (aStarred !== bStarred) return aStarred ? -1 : 1;
      if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
      return 0;
    });
  }, [starredSinks]);

  useEffect(() => {
    setSortedSinks(sortSinks(sinks));
  }, [sinks, starredSinks, sortSinks]);

  const jumpToAnchor = useCallback((name: string) => {
    const element = sinkRefs.current[name];
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

    const newSinks = [...sinks];
    const [reorderedItem] = newSinks.splice(draggedIndex, 1);
    newSinks.splice(targetIndex, 0, reorderedItem);

    setSinks(newSinks);
    setSortedSinks(sortSinks(newSinks));

    try {
      await ApiService.reorderSink(reorderedItem.name, targetIndex);
      await fetchSinks();
      jumpToAnchor(reorderedItem.name);
    } catch (error) {
      console.error('Error reordering sink:', error);
      setError('Failed to reorder sink. Please try again.');
    }

    dragItem.current = null;
    dragOverItem.current = null;
  };

  const getActiveRoutes = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName && route.enabled);
  };

  const getDisabledRoutes = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName && !route.enabled);
  };

  const toggleExpandRoutes = (name: string) => {
    setExpandedRoutes(prev =>
      prev.includes(name) ? prev.filter(n => n !== name) : [...prev, name]
    );
  };

  return (
    <div className="sinks">
      <h2>Sinks</h2>
      {error && <div className="error-message">{error}</div>}
      <div className="actions">
        <button onClick={() => setShowAddModal(true)}>Add Sink</button>
        <button onClick={() => setShowGroupModal(true)}>Add Group</button>
      </div>
      <SinkList
        sinks={sortedSinks}
        routes={routes}
        starredSinks={starredSinks}
        onToggleSink={toggleSink}
        onDeleteSink={deleteSink}
        onUpdateVolume={updateVolume}
        onToggleStar={toggleStar}
        onEditSink={(sink) => { setSelectedSink(sink); setShowEditModal(true); }}
        onShowEqualizer={(sink) => { setSelectedSink(sink); setShowEqualizerModal(true); }}
        sinkRefs={sinkRefs}
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
            <AddEditSink
              onClose={() => setShowAddModal(false)}
              onSubmit={() => { fetchSinks(); setShowAddModal(false); }}
            />
          </div>
        </div>
      )}

      {showEditModal && selectedSink && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEditModal(false)}>×</button>
            <AddEditSink
              sink={selectedSink}
              onClose={() => setShowEditModal(false)}
              onSubmit={() => { fetchSinks(); setShowEditModal(false); }}
            />
          </div>
        </div>
      )}

      {showGroupModal && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowGroupModal(false)}>×</button>
            <AddEditGroup
              type="sink"
              group={selectedSink}
              onClose={() => setShowGroupModal(false)}
              onSubmit={() => { fetchSinks(); setShowGroupModal(false); }}
            />
          </div>
        </div>
      )}

      {showEqualizerModal && selectedSink && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEqualizerModal(false)}>×</button>
            <Equalizer
              item={selectedSink}
              type="sinks"
              onClose={() => setShowEqualizerModal(false)}
              onDataChange={fetchSinks}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Sinks;
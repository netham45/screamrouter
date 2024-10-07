import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Link, useLocation } from 'react-router-dom';
import ApiService, { Sink, Route } from '../api/api';
import Equalizer from './Equalizer';
import AddEditSink from './AddEditSink';
import AddEditGroup from './AddEditGroup';

declare global {
  interface Window {
    startVisualizer: (sinkIp: string) => void;
    stopVisualizer: () => void;
    canvasClick: () => void;
    canvasOnKeyDown: (e: KeyboardEvent) => void;
  }
}

const Sinks: React.FC = () => {
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [showAddModal, setShowAddModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showGroupModal, setShowGroupModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedSink, setSelectedSink] = useState<Sink | undefined>(undefined);
  const [listeningToSink, setListeningToSink] = useState<string | null>(null);
  const [visualizingSink, setVisualizingSink] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  const sinkRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const [expandedRoutes, setExpandedRoutes] = useState<string[]>([]);
  const location = useLocation();
  const dragItem = useRef<number | null>(null);
  const dragOverItem = useRef<number | null>(null);
  const audioRef = useRef<HTMLAudioElement>(null);
  const visualizerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    fetchSinks();
    fetchRoutes();
    const starred = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    setStarredSinks(starred);

    const canvas = document.getElementById('canvas');
    if (canvas) {
      canvas.addEventListener('click', window.canvasClick);
      canvas.addEventListener('keydown', window.canvasOnKeyDown);
    }

    return () => {
      if (canvas) {
        canvas.removeEventListener('click', window.canvasClick);
        canvas.removeEventListener('keydown', window.canvasOnKeyDown);
      }
    };
  }, []);

  useEffect(() => {
    const hash = location.hash;
    if (hash) {
      const sinkName = decodeURIComponent(hash.replace('#sink-', ''));
      jumpToAnchor(sinkName);
    }
  }, [location.hash, sinks]);

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

  const toggleSink = async (name: string) => {
    try {
      const sinkToUpdate = sinks.find(sink => sink.name === name);
      if (sinkToUpdate) {
        if (sinkToUpdate.enabled) {
          await ApiService.disableSink(name);
        } else {
          await ApiService.enableSink(name);
        }
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

  const listenToSink = (sink: Sink) => {
    if (listeningToSink === sink.name) {
      setListeningToSink(null);
      if (audioRef.current) {
        audioRef.current.pause();
        audioRef.current.src = '';
      }
    } else {
      setListeningToSink(sink.name);
      if (audioRef.current) {
        audioRef.current.src = ApiService.getSinkStreamUrl(sink.ip);
        audioRef.current.play();
      }
    }
  };

  const visualizeSink = (sink: Sink) => {
    if (visualizingSink === sink.name) {
      setVisualizingSink(null);
      if (window.stopVisualizer) {
        window.stopVisualizer();
      }
    } else {
      setVisualizingSink(sink.name);
      if (window.startVisualizer) {
        window.startVisualizer(sink.ip);
        setTimeout(() => {
          if (visualizerRef.current) {
            visualizerRef.current.scrollIntoView({ behavior: 'smooth' });
          }
        }, 100);
      }
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
      if (aStarred && !bStarred) return -1;
      if (!aStarred && bStarred) return 1;
      if (a.enabled && !b.enabled) return -1;
      if (!a.enabled && b.enabled) return 1;
      return 0;
    });
  }, [starredSinks]);

  const [sortedSinks, setSortedSinks] = useState<Sink[]>([]);

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

  const getSinkStatus = (sink: Sink) => {
    if (starredSinks.includes(sink.name)) {
      return sink.enabled ? 'starredEnabled' : 'starredDisabled';
    }
    return sink.enabled ? 'enabled' : 'disabled';
  };

  const findClosestSameStatusSink = (sinks: Sink[], targetIndex: number, status: string) => {
    let aboveIndex = -1;
    let belowIndex = -1;

    // Search above
    for (let i = targetIndex - 1; i >= 0; i--) {
      if (getSinkStatus(sinks[i]) === status) {
        aboveIndex = i;
        break;
      }
    }

    // Search below
    for (let i = targetIndex; i < sinks.length; i++) {
      if (getSinkStatus(sinks[i]) === status) {
        belowIndex = i;
        break;
      }
    }

    if (aboveIndex === -1 && belowIndex === -1) return -1;
    if (aboveIndex === -1) return belowIndex;
    if (belowIndex === -1) return aboveIndex;

    return targetIndex - aboveIndex <= belowIndex - targetIndex ? aboveIndex : belowIndex;
  };

  const onDrop = async (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    const draggedIndex = dragItem.current;

    if (draggedIndex === null || draggedIndex === targetIndex) {
      return;
    }

    const newSinks = [...sinks];
    const draggedSink = newSinks[draggedIndex];
    const draggedStatus = getSinkStatus(draggedSink);

    const closestSameStatusIndex = findClosestSameStatusSink(newSinks, targetIndex, draggedStatus);

    let finalIndex: number;

    if (closestSameStatusIndex === -1) {
      finalIndex = targetIndex;
    } else if (closestSameStatusIndex < draggedIndex) {
      finalIndex = closestSameStatusIndex + 1;
    } else {
      finalIndex = closestSameStatusIndex;
    }

    newSinks.splice(draggedIndex, 1);
    newSinks.splice(finalIndex, 0, draggedSink);

    setSinks(newSinks);
    setSortedSinks(sortSinks(newSinks));

    try {
      await ApiService.reorderSink(draggedSink.name, finalIndex);
      await fetchSinks();
      jumpToAnchor(draggedSink.name);
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

  const renderRoutes = (sinkName: string) => {
    const activeRoutes = getActiveRoutes(sinkName);
    const disabledRoutes = getDisabledRoutes(sinkName);
    const isExpanded = expandedRoutes.includes(sinkName);

    const renderRouteList = (routes: Route[], isEnabled: boolean) => {
      if (routes.length === 0) return null;

      const displayedRoutes = isExpanded ? routes : routes.slice(0, 3);
      const hasMore = routes.length > 3;

      return (
        <div className={`route-list ${isEnabled ? 'enabled' : 'disabled'}`}>
          <span className="route-list-label">{isEnabled ? 'Enabled routes:' : 'Disabled routes:'}</span>
          {displayedRoutes.map((route, index) => (
            <React.Fragment key={route.name}>
              <Link
                to={`/routes#route-${encodeURIComponent(route.name)}`}
                className="route-link"
              >
                {route.name}
              </Link>
              {index < displayedRoutes.length - 1 && ', '}
            </React.Fragment>
          ))}
          {hasMore && !isExpanded && (
            <button onClick={() => toggleExpandRoutes(sinkName)} className="expand-routes">
              ...
            </button>
          )}
        </div>
      );
    };

    return (
      <div className="sink-routes">
        {renderRouteList(activeRoutes, true)}
        {renderRouteList(disabledRoutes, false)}
        {isExpanded && (
          <button onClick={() => toggleExpandRoutes(sinkName)} className="collapse-routes">
            Show less
          </button>
        )}
      </div>
    );
  };

  const renderGroupMembers = (sink: Sink) => {
    if (!sink.is_group || !sink.group_members || sink.group_members.length === 0) {
      return null;
    }

    return (
      <div className="group-members">
        <span>Group members: </span>
        {sink.group_members.map((member, index) => (
          <React.Fragment key={member}>
            <Link to={`#sink-${encodeURIComponent(member)}`} onClick={(e) => { e.preventDefault(); jumpToAnchor(member); }}>
              {member}
            </Link>
            {index < sink.group_members.length - 1 && ', '}
          </React.Fragment>
        ))}
      </div>
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
      <table className="sinks-table">
        <thead>
          <tr>
            <th>Reorder</th>
            <th>Favorite</th>
            <th>Name</th>
            <th>IP Address</th>
            <th>Port</th>
            <th>Status</th>
            <th>Volume</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedSinks.map((sink, index) => (
            <tr
              key={sink.name}
              ref={(el) => {
                if (el) sinkRefs.current[sink.name] = el;
              }}
              onDragEnter={(e) => onDragEnter(e, index)}
              onDragLeave={onDragLeave}
              onDragOver={onDragOver}
              onDrop={(e) => onDrop(e, index)}
              className="draggable-row"
              id={`sink-${encodeURIComponent(sink.name)}`}
            >
              <td>
                <span
                  className="drag-handle"
                  draggable
                  onDragStart={(e) => onDragStart(e, index)}
                  onDragEnd={onDragEnd}
                >
                  ☰
                </span>
              </td>
              <td>
                <button onClick={() => toggleStar(sink.name)}>
                  {starredSinks.includes(sink.name) ? '★' : '☆'}
                </button>
              </td>
              <td>
                <Link 
                  to={`#sink-${encodeURIComponent(sink.name)}`} 
                  onClick={(e) => { e.preventDefault(); jumpToAnchor(sink.name); }}
                  className="sink-name"
                >
                  {sink.name} {sink.is_group && '(Group)'}
                </Link>
                {renderGroupMembers(sink)}
                {renderRoutes(sink.name)}
              </td>
              <td>{sink.ip}</td>
              <td>{sink.port}</td>
              <td>
                <button 
                  onClick={() => toggleSink(sink.name)}
                  className={sink.enabled ? 'enabled' : 'disabled'}
                >
                  {sink.enabled ? 'Enabled' : 'Disabled'}
                </button>
              </td>
              <td>
                <input
                  type="range"
                  min="0"
                  max="1"
                  step="0.01"
                  value={sink.volume}
                  onChange={(e) => updateVolume(sink.name, parseFloat(e.target.value))}
                />
                <span>{(sink.volume * 100).toFixed(0)}%</span>
              </td>
              <td>
                <button onClick={() => {
                  setSelectedSink(sink);
                  if (sink.is_group) {
                    setShowGroupModal(true);
                  } else {
                    setShowEditModal(true);
                  }
                }}>Edit</button>
                <button onClick={() => { setSelectedSink(sink); setShowEqualizerModal(true); }}>Equalizer</button>
                <button 
                  onClick={() => listenToSink(sink)}
                  className={listeningToSink === sink.name ? 'listening' : ''}
                >
                  {listeningToSink === sink.name ? 'Stop Listening' : 'Listen'}
                </button>
                <button 
                  onClick={() => visualizeSink(sink)}
                  className={visualizingSink === sink.name ? 'visualizing' : ''}
                >
                  {visualizingSink === sink.name ? 'Stop Visualizer' : 'Visualize'}
                </button>
                <button onClick={() => deleteSink(sink.name)} className="delete-button">Delete</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

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
            />
          </div>
        </div>
      )}

      <audio ref={audioRef} style={{ display: 'none' }} />
      
      <div ref={visualizerRef} className="visualizer-container">
        <div id="mainWrapper" style={{ display: visualizingSink ? 'block' : 'none', width: '30%' }}>
          <div id="presetControls">
            <div>Preset: <select id="presetSelect"></select></div>
            <div>
              Cycle:
              <input type="checkbox" id="presetCycle" defaultChecked />
              <input type="number" id="presetCycleLength" step="1" defaultValue="15" min="1" />
            </div>
            <div>Random: <input type="checkbox" id="presetRandom" defaultChecked /></div>
          </div>
          <canvas id="canvas" width="3840" height="2160"></canvas>
        </div>
      </div>
    </div>
  );
};

export default Sinks;
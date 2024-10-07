import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Link, useLocation } from 'react-router-dom';
import ApiService, { Source, Route } from '../api/api';
import Equalizer from './Equalizer';
import AddEditSource from './AddEditSource';
import AddEditGroup from './AddEditGroup';
import VNC from './VNC';

const Sources: React.FC = () => {
  const [sources, setSources] = useState<Source[]>([]);
  const [routes, setRoutes] = useState<Route[]>([]);
  const [showAddModal, setShowAddModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showGroupModal, setShowGroupModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [showVNCModal, setShowVNCModal] = useState(false);
  const [selectedSource, setSelectedSource] = useState<Source | undefined>(undefined);
  const [error, setError] = useState<string | null>(null);
  const [starredSources, setStarredSources] = useState<string[]>([]);
  const [activeSource, setActiveSource] = useState<string | null>(null);
  const sourceRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const [expandedRoutes, setExpandedRoutes] = useState<string[]>([]);
  const location = useLocation();
  const dragItem = useRef<number | null>(null);
  const dragOverItem = useRef<number | null>(null);

  const jumpToAnchor = useCallback((name: string) => {
    const element = sourceRefs.current[name];
    if (element) {
      element.scrollIntoView({ behavior: 'smooth', block: 'center' });
      element.classList.remove('flash');
      void element.offsetWidth; // Trigger reflow
      element.classList.add('flash');
    }
  }, []);

  useEffect(() => {
    fetchSources();
    fetchRoutes();
    const starred = JSON.parse(localStorage.getItem('starredSources') || '[]');
    setStarredSources(starred);
    const active = localStorage.getItem('activeSource');
    setActiveSource(active);
  }, []);

  useEffect(() => {
    const hash = location.hash;
    if (hash) {
      const sourceName = decodeURIComponent(hash.replace('#source-', ''));
      jumpToAnchor(sourceName);
    }
  }, [location.hash, sources, jumpToAnchor]);

  const fetchSources = async () => {
    try {
      const response = await ApiService.getSources();
      setSources(response.data);
      setError(null);
    } catch (error) {
      console.error('Error fetching sources:', error);
      setError('Failed to fetch sources. Please try again later.');
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

  const toggleSource = async (name: string) => {
    try {
      const sourceToUpdate = sources.find(source => source.name === name);
      if (sourceToUpdate) {
        if (sourceToUpdate.enabled) {
          await ApiService.disableSource(name);
        } else {
          await ApiService.enableSource(name);
        }
        await fetchSources();
        jumpToAnchor(name);
      }
    } catch (error) {
      console.error('Error updating source:', error);
      setError('Failed to update source. Please try again.');
    }
  };

  const deleteSource = async (name: string) => {
    if (window.confirm(`Are you sure you want to delete the source "${name}"?`)) {
      try {
        await ApiService.deleteSource(name);
        await fetchSources();
      } catch (error) {
        console.error('Error deleting source:', error);
        setError('Failed to delete source. Please try again.');
      }
    }
  };

  const updateVolume = async (name: string, volume: number) => {
    try {
      await ApiService.updateSourceVolume(name, volume);
      await fetchSources();
    } catch (error) {
      console.error('Error updating source volume:', error);
      setError('Failed to update volume. Please try again.');
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

  const toggleActive = (name: string) => {
    const newActiveSource = activeSource === name ? null : name;
    setActiveSource(newActiveSource);
    localStorage.setItem('activeSource', newActiveSource || '');
  };

  const controlSource = async (sourceName: string, action: 'prevtrack' | 'play' | 'nexttrack') => {
    try {
      await fetch(`https://screamrouter.netham45.org/sources/${sourceName}/${action}`, { method: 'GET' });
      // Optionally update the UI or refetch data if needed
    } catch (error) {
      console.error(`Error controlling source: ${error}`);
      setError(`Failed to control source. Please try again.`);
    }
  };

  const sortSources = useCallback((sourcesToSort: Source[]) => {
    return [...sourcesToSort].sort((a, b) => {
      const aStarred = starredSources.includes(a.name);
      const bStarred = starredSources.includes(b.name);
      if (aStarred && !bStarred) return -1;
      if (!aStarred && bStarred) return 1;
      if (a.enabled && !b.enabled) return -1;
      if (!a.enabled && b.enabled) return 1;
      return 0;
    });
  }, [starredSources]);

  const [sortedSources, setSortedSources] = useState<Source[]>([]);

  useEffect(() => {
    setSortedSources(sortSources(sources));
  }, [sources, starredSources, sortSources]);

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

  const getSourceStatus = (source: Source) => {
    if (starredSources.includes(source.name)) {
      return source.enabled ? 'starredEnabled' : 'starredDisabled';
    }
    return source.enabled ? 'enabled' : 'disabled';
  };

  const findClosestSameStatusSource = (sources: Source[], targetIndex: number, status: string) => {
    let aboveIndex = -1;
    let belowIndex = -1;

    // Search above
    for (let i = targetIndex - 1; i >= 0; i--) {
      if (getSourceStatus(sources[i]) === status) {
        aboveIndex = i;
        break;
      }
    }

    // Search below
    for (let i = targetIndex; i < sources.length; i++) {
      if (getSourceStatus(sources[i]) === status) {
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

    const newSources = [...sources];
    const draggedSource = newSources[draggedIndex];
    const draggedStatus = getSourceStatus(draggedSource);

    const closestSameStatusIndex = findClosestSameStatusSource(newSources, targetIndex, draggedStatus);

    let finalIndex: number;

    if (closestSameStatusIndex === -1) {
      finalIndex = targetIndex;
    } else if (closestSameStatusIndex < draggedIndex) {
      finalIndex = closestSameStatusIndex + 1;
    } else {
      finalIndex = closestSameStatusIndex;
    }

    newSources.splice(draggedIndex, 1);
    newSources.splice(finalIndex, 0, draggedSource);

    setSources(newSources);
    setSortedSources(sortSources(newSources));

    try {
      await ApiService.reorderSource(draggedSource.name, finalIndex);
      await fetchSources();
      jumpToAnchor(draggedSource.name);
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

  const renderRoutes = (sourceName: string) => {
    const activeRoutes = getActiveRoutes(sourceName);
    const disabledRoutes = getDisabledRoutes(sourceName);
    const isExpanded = expandedRoutes.includes(sourceName);

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
            <button onClick={() => toggleExpandRoutes(sourceName)} className="expand-routes">
              ...
            </button>
          )}
        </div>
      );
    };

    return (
      <div className="source-routes">
        {renderRouteList(activeRoutes, true)}
        {renderRouteList(disabledRoutes, false)}
        {isExpanded && (
          <button onClick={() => toggleExpandRoutes(sourceName)} className="collapse-routes">
            Show less
          </button>
        )}
      </div>
    );
  };

  const renderGroupMembers = (source: Source) => {
    if (!source.is_group || !source.group_members || source.group_members.length === 0) {
      return null;
    }

    return (
      <div className="group-members">
        <span>Group members: </span>
        {source.group_members.map((member, index) => (
          <React.Fragment key={member}>
            <Link to={`#source-${encodeURIComponent(member)}`} onClick={(e) => { e.preventDefault(); jumpToAnchor(member); }}>
              {member}
            </Link>
            {index < source.group_members.length - 1 && ', '}
          </React.Fragment>
        ))}
      </div>
    );
  };

  return (
    <div className="sources">
      <h2>Sources</h2>
      {error && <div className="error-message">{error}</div>}
      <div className="actions">
        <button onClick={() => setShowAddModal(true)}>Add Source</button>
        <button onClick={() => setShowGroupModal(true)}>Add Group</button>
      </div>
      <table className="sources-table">
        <thead>
          <tr>
            <th>Reorder</th>
            <th>Favorite</th>
            <th>Active</th>
            <th>Name</th>
            <th>IP Address</th>
            <th>Status</th>
            <th>Volume</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedSources.map((source, index) => (
            <tr
              key={source.name}
              ref={(el) => {
                if (el) sourceRefs.current[source.name] = el;
              }}
              onDragEnter={(e) => onDragEnter(e, index)}
              onDragLeave={onDragLeave}
              onDragOver={onDragOver}
              onDrop={(e) => onDrop(e, index)}
              className="draggable-row"
              id={`source-${encodeURIComponent(source.name)}`}
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
                <button onClick={() => toggleStar(source.name)}>
                  {starredSources.includes(source.name) ? '★' : '☆'}
                </button>
              </td>
              <td>
                <button 
                  onClick={() => toggleActive(source.name)}
                  className={activeSource === source.name ? 'active' : ''}
                >
                  {activeSource === source.name ? '⬤' : '◯'}
                </button>
              </td>
              <td>
                <Link 
                  to={`#source-${encodeURIComponent(source.name)}`} 
                  onClick={(e) => { e.preventDefault(); jumpToAnchor(source.name); }}
                  className="source-name"
                >
                  {source.name} {source.is_group && '(Group)'}
                </Link>
                {renderGroupMembers(source)}
                {renderRoutes(source.name)}
              </td>
              <td>{source.ip}</td>
              <td>
                <button 
                  onClick={() => toggleSource(source.name)}
                  className={source.enabled ? 'enabled' : 'disabled'}
                >
                  {source.enabled ? 'Enabled' : 'Disabled'}
                </button>
              </td>
              <td>
                <input
                  type="range"
                  min="0"
                  max="1"
                  step="0.01"
                  value={source.volume}
                  onChange={(e) => updateVolume(source.name, parseFloat(e.target.value))}
                />
                <span>{(source.volume * 100).toFixed(0)}%</span>
              </td>
              <td>
                <button onClick={() => {
                  setSelectedSource(source);
                  if (source.is_group) {
                    setShowGroupModal(true);
                  } else {
                    setShowEditModal(true);
                  }
                }}>Edit</button>
                <button onClick={() => { setSelectedSource(source); setShowEqualizerModal(true); }}>Equalizer</button>
                {source.vnc_ip && source.vnc_port && (
                  <>
                    <button onClick={() => { setSelectedSource(source); setShowVNCModal(true); }}>VNC</button>
                    <button onClick={() => controlSource(source.name, 'prevtrack')}>⏮</button>
                    <button onClick={() => controlSource(source.name, 'play')}>⏯</button>
                    <button onClick={() => controlSource(source.name, 'nexttrack')}>⏭</button>
                  </>
                )}
                <button onClick={() => deleteSource(source.name)} className="delete-button">Delete</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

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
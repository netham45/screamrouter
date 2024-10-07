import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Link, useLocation } from 'react-router-dom';
import ApiService, { Route } from '../api/api';
import Equalizer from './Equalizer';
import AddEditRoute from './AddEditRoute';

const Routes: React.FC = () => {
  const [routes, setRoutes] = useState<Route[]>([]);
  const [showAddModal, setShowAddModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedRoute, setSelectedRoute] = useState<Route | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);
  const routeRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const [expandedRoutes, setExpandedRoutes] = useState<string[]>([]);
  const location = useLocation();
  const dragItem = useRef<number | null>(null);
  const dragOverItem = useRef<number | null>(null);

  useEffect(() => {
    fetchRoutes();
    const starred = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
    setStarredRoutes(starred);
  }, []);

  useEffect(() => {
    const hash = location.hash;
    if (hash) {
      const routeName = decodeURIComponent(hash.replace('#route-', ''));
      jumpToAnchor(routeName);
    }
  }, [location.hash, routes]);

  const fetchRoutes = async () => {
    try {
      const response = await ApiService.getRoutes();
      setRoutes(response.data);
      setError(null);
    } catch (error) {
      console.error('Error fetching routes:', error);
      setError('Failed to fetch routes. Please try again later.');
    }
  };

  const toggleRoute = async (name: string) => {
    try {
      const routeToUpdate = routes.find(route => route.name === name);
      if (routeToUpdate) {
        if (routeToUpdate.enabled) {
          await ApiService.disableRoute(name);
        } else {
          await ApiService.enableRoute(name);
        }
        await fetchRoutes();
        jumpToAnchor(name);
      }
    } catch (error) {
      console.error('Error updating route:', error);
      setError('Failed to update route. Please try again.');
    }
  };

  const deleteRoute = async (name: string) => {
    if (window.confirm(`Are you sure you want to delete the route "${name}"?`)) {
      try {
        await ApiService.deleteRoute(name);
        await fetchRoutes();
      } catch (error) {
        console.error('Error deleting route:', error);
        setError('Failed to delete route. Please try again.');
      }
    }
  };

  const updateVolume = async (name: string, volume: number) => {
    try {
      await ApiService.updateRouteVolume(name, volume);
      await fetchRoutes();
    } catch (error) {
      console.error('Error updating route volume:', error);
      setError('Failed to update volume. Please try again.');
    }
  };

  const toggleStar = (name: string) => {
    const newStarredRoutes = starredRoutes.includes(name)
      ? starredRoutes.filter(route => route !== name)
      : [...starredRoutes, name];
    setStarredRoutes(newStarredRoutes);
    localStorage.setItem('starredRoutes', JSON.stringify(newStarredRoutes));
    jumpToAnchor(name);
  };

  const sortRoutes = useCallback((routesToSort: Route[]) => {
    return [...routesToSort].sort((a, b) => {
      const aStarred = starredRoutes.includes(a.name);
      const bStarred = starredRoutes.includes(b.name);
      if (aStarred && !bStarred) return -1;
      if (!aStarred && bStarred) return 1;
      if (a.enabled && !b.enabled) return -1;
      if (!a.enabled && b.enabled) return 1;
      return 0;
    });
  }, [starredRoutes]);

  const [sortedRoutes, setSortedRoutes] = useState<Route[]>([]);

  useEffect(() => {
    setSortedRoutes(sortRoutes(routes));
  }, [routes, starredRoutes, sortRoutes]);

  const jumpToAnchor = useCallback((name: string) => {
    const element = routeRefs.current[name];
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

  const getRouteStatus = (route: Route) => {
    if (starredRoutes.includes(route.name)) {
      return route.enabled ? 'starredEnabled' : 'starredDisabled';
    }
    return route.enabled ? 'enabled' : 'disabled';
  };

  const findClosestSameStatusRoute = (routes: Route[], targetIndex: number, status: string) => {
    let aboveIndex = -1;
    let belowIndex = -1;

    // Search above
    for (let i = targetIndex - 1; i >= 0; i--) {
      if (getRouteStatus(routes[i]) === status) {
        aboveIndex = i;
        break;
      }
    }

    // Search below
    for (let i = targetIndex; i < routes.length; i++) {
      if (getRouteStatus(routes[i]) === status) {
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

    const newRoutes = [...routes];
    const draggedRoute = newRoutes[draggedIndex];
    const draggedStatus = getRouteStatus(draggedRoute);

    const closestSameStatusIndex = findClosestSameStatusRoute(newRoutes, targetIndex, draggedStatus);

    let finalIndex: number;

    if (closestSameStatusIndex === -1) {
      finalIndex = targetIndex;
    } else if (closestSameStatusIndex < draggedIndex) {
      finalIndex = closestSameStatusIndex + 1;
    } else {
      finalIndex = closestSameStatusIndex;
    }

    newRoutes.splice(draggedIndex, 1);
    newRoutes.splice(finalIndex, 0, draggedRoute);

    setRoutes(newRoutes);
    setSortedRoutes(sortRoutes(newRoutes));

    try {
      await ApiService.reorderRoute(draggedRoute.name, finalIndex);
      await fetchRoutes();
      jumpToAnchor(draggedRoute.name);
    } catch (error) {
      console.error('Error reordering route:', error);
      setError('Failed to reorder route. Please try again.');
    }

    dragItem.current = null;
    dragOverItem.current = null;
  };

  const toggleExpandRoute = (name: string) => {
    setExpandedRoutes(prev =>
      prev.includes(name) ? prev.filter(n => n !== name) : [...prev, name]
    );
  };

  const renderRouteDetails = (route: Route) => {
    const isExpanded = expandedRoutes.includes(route.name);
    const details = `Source: ${route.source}, Sink: ${route.sink}`;
    const truncatedDetails = details.length > 30 ? `${details.slice(0, 30)}...` : details;

    return (
      <div className="route-details">
        {isExpanded ? (
          <span>{details}</span>
        ) : (
          <span>
            {truncatedDetails}
            {details.length > 30 && (
              <button onClick={() => toggleExpandRoute(route.name)} className="expand-details">
                ...
              </button>
            )}
          </span>
        )}
      </div>
    );
  };

  return (
    <div className="routes">
      <h2>Routes</h2>
      {error && <div className="error-message">{error}</div>}
      <div className="actions">
        <button onClick={() => setShowAddModal(true)}>Add Route</button>
      </div>
      <table className="routes-table">
        <thead>
          <tr>
            <th>Reorder</th>
            <th>Favorite</th>
            <th>Name</th>
            <th>Status</th>
            <th>Volume</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedRoutes.map((route, index) => (
            <tr
              key={route.name}
              ref={(el) => {
                if (el) routeRefs.current[route.name] = el;
              }}
              onDragEnter={(e) => onDragEnter(e, index)}
              onDragLeave={onDragLeave}
              onDragOver={onDragOver}
              onDrop={(e) => onDrop(e, index)}
              className="draggable-row"
              id={`route-${encodeURIComponent(route.name)}`}
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
                <button onClick={() => toggleStar(route.name)}>
                  {starredRoutes.includes(route.name) ? '★' : '☆'}
                </button>
              </td>
              <td>
                <Link 
                  to={`#route-${encodeURIComponent(route.name)}`} 
                  onClick={(e) => { e.preventDefault(); jumpToAnchor(route.name); }}
                  className="route-name"
                >
                  {route.name}
                </Link>
                {renderRouteDetails(route)}
              </td>
              <td>
                <button 
                  onClick={() => toggleRoute(route.name)}
                  className={route.enabled ? 'enabled' : 'disabled'}
                >
                  {route.enabled ? 'Enabled' : 'Disabled'}
                </button>
              </td>
              <td>
                <input
                  type="range"
                  min="0"
                  max="1"
                  step="0.01"
                  value={route.volume}
                  onChange={(e) => updateVolume(route.name, parseFloat(e.target.value))}
                />
                <span>{(route.volume * 100).toFixed(0)}%</span>
              </td>
              <td>
                <button onClick={() => { setSelectedRoute(route); setShowEditModal(true); }}>Edit</button>
                <button onClick={() => { setSelectedRoute(route); setShowEqualizerModal(true); }}>Equalizer</button>
                <button onClick={() => deleteRoute(route.name)} className="delete-button">Delete</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      {showAddModal && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowAddModal(false)}>×</button>
            <AddEditRoute
              onClose={() => setShowAddModal(false)}
              onSubmit={() => { fetchRoutes(); setShowAddModal(false); }}
            />
          </div>
        </div>
      )}

      {showEditModal && selectedRoute && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEditModal(false)}>×</button>
            <AddEditRoute
              route={selectedRoute}
              onClose={() => setShowEditModal(false)}
              onSubmit={() => { fetchRoutes(); setShowEditModal(false); }}
            />
          </div>
        </div>
      )}

      {showEqualizerModal && selectedRoute && (
        <div className="modal-overlay">
          <div className="modal-content">
            <button className="close-modal" onClick={() => setShowEqualizerModal(false)}>×</button>
            <Equalizer
              item={selectedRoute}
              type="routes"
              onClose={() => setShowEqualizerModal(false)}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Routes;
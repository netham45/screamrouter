import React, { useState, useEffect, useRef, useCallback } from 'react';
import { useLocation, Link } from 'react-router-dom';
import ApiService, { Route } from '../api/api';
import Equalizer from './Equalizer';
import AddEditRoute from './AddEditRoute';
import RouteList from './RouteList';

const Routes: React.FC = () => {
  const [routes, setRoutes] = useState<Route[]>([]);
  const [showAddModal, setShowAddModal] = useState(false);
  const [showEditModal, setShowEditModal] = useState(false);
  const [showEqualizerModal, setShowEqualizerModal] = useState(false);
  const [selectedRoute, setSelectedRoute] = useState<Route | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);
  const [sortedRoutes, setSortedRoutes] = useState<Route[]>([]);

  const routeRefs = useRef<{[key: string]: HTMLTableRowElement}>({});
  const dragItem = useRef<number | null>(null);
  const dragOverItem = useRef<number | null>(null);

  const location = useLocation();

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

  const toggleRoute = async (name: string) => {
    try {
      const routeToUpdate = routes.find(route => route.name === name);
      if (routeToUpdate) {
        await ApiService[routeToUpdate.enabled ? 'disableRoute' : 'enableRoute'](name);
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
      if (aStarred !== bStarred) return aStarred ? -1 : 1;
      if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
      return 0;
    });
  }, [starredRoutes]);

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

  const onDrop = async (e: React.DragEvent<HTMLTableRowElement>, targetIndex: number) => {
    e.preventDefault();
    const draggedIndex = dragItem.current;

    if (draggedIndex === null || draggedIndex === targetIndex) {
      return;
    }

    const newRoutes = [...routes];
    const [reorderedItem] = newRoutes.splice(draggedIndex, 1);
    newRoutes.splice(targetIndex, 0, reorderedItem);

    setRoutes(newRoutes);
    setSortedRoutes(sortRoutes(newRoutes));

    try {
      await ApiService.reorderRoute(reorderedItem.name, targetIndex);
      await fetchRoutes();
      jumpToAnchor(reorderedItem.name);
    } catch (error) {
      console.error('Error reordering route:', error);
      setError('Failed to reorder route. Please try again.');
    }

    dragItem.current = null;
    dragOverItem.current = null;
  };

  const renderLinkWithAnchor = (to: string, name: string, icon: string) => (
    <Link to={`${to}#${to.replace(/^\//,'').replace(/s$/,'')}-${encodeURIComponent(name)}`}>
      <i className={`fas ${icon}`}></i> {name}
    </Link>
  );

  return (
    <div className="routes">
      <h2>Routes</h2>
      {error && <div className="error-message">{error}</div>}
      <div className="actions">
        <button onClick={() => setShowAddModal(true)}>Add Route</button>
      </div>
      <RouteList
        routes={sortedRoutes}
        starredRoutes={starredRoutes}
        onToggleRoute={toggleRoute}
        onDeleteRoute={deleteRoute}
        onUpdateVolume={updateVolume}
        onToggleStar={toggleStar}
        onEditRoute={(route) => { setSelectedRoute(route); setShowEditModal(true); }}
        onShowEqualizer={(route) => { setSelectedRoute(route); setShowEqualizerModal(true); }}
        routeRefs={routeRefs}
        onDragStart={onDragStart}
        onDragEnter={onDragEnter}
        onDragLeave={onDragLeave}
        onDragOver={onDragOver}
        onDrop={onDrop}
        onDragEnd={onDragEnd}
        jumpToAnchor={jumpToAnchor}
        renderLinkWithAnchor={renderLinkWithAnchor}
      />
      
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
              onDataChange={fetchRoutes}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default Routes;
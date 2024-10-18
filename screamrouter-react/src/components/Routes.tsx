import React, { useState, useEffect } from 'react';
import RouteList from './RouteList';
import { useAppContext } from '../context/AppContext';
import { Actions } from '../utils/actions';
import ActionButton from './controls/ActionButton';
import { SortConfig, useAnchorFlash } from '../utils/commonUtils';
import ConfirmationModal from './ConfirmationModal';
import ApiService from '../api/api';

const Routes: React.FC = () => {
  useAnchorFlash();

  const { 
    routes, 
    fetchRoutes, 
    toggleEnabled, 
    updateVolume, 
    updateTimeshift, 
    openEqualizerModal,
    setSelectedItem,
    setSelectedItemType,
    setShowEditModal
  } = useAppContext();
  
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: 'name', direction: 'asc' });
  const [showDeleteModal, setShowDeleteModal] = useState(false);
  const [routeToDelete, setRouteToDelete] = useState<string | null>(null);

  useEffect(() => {
    if (routes.length === 0) {
      loadRoutes();
    }
    const savedStarredRoutes = localStorage.getItem('starredRoutes');
    if (savedStarredRoutes) {
      setStarredRoutes(JSON.parse(savedStarredRoutes));
    }
  }, []);

  const loadRoutes = async () => {
    setIsLoading(true);
    setError(null);
    try {
      await fetchRoutes();
    } catch (err) {
      console.error('Error fetching routes:', err);
      setError('Failed to fetch routes. Please try again.');
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
    setStarredRoutes(prev => {
      const newStarred = prev.includes(name)
        ? prev.filter(route => route !== name)
        : [...prev, name];
      localStorage.setItem('starredRoutes', JSON.stringify(newStarred));
      return newStarred;
    });
  };

  const handleAddRoute = () => {
    setSelectedItem(null);
    setSelectedItemType('routes');
    setShowEditModal(true);
  };

  const handleDeleteRoute = async () => {
    if (routeToDelete) {
      try {
        await ApiService.deleteRoute(routeToDelete);
        console.log(`Route deleted: ${routeToDelete}`);
        await fetchRoutes();
      } catch (error) {
        console.error('Error deleting route:', error);
        setError('Failed to delete route. Please try again.');
      }
    }
    setShowDeleteModal(false);
    setRouteToDelete(null);
  };

  const actions: Actions = {
    toggleEnabled: async (type, name) => {
      if (type === 'routes') {
        const route = routes.find(r => r.name === name);
        if (route) {
          await toggleEnabled('routes', name, route.enabled);
        }
      }
    },
    updateVolume: (type, name, volume) => updateVolume(type, name, volume),
    updateTimeshift: (type, name, timeshift) => updateTimeshift(type, name, timeshift),
    controlSource: async () => {}, // Not applicable for routes
    toggleStar: async (type, name) => {
      if (type === 'routes') await onToggleStar(name);
    },
    deleteItem: async (type, name) => {
      if (type === 'routes') {
        setRouteToDelete(name);
        setShowDeleteModal(true);
      }
    },
    editItem: (type, item) => {
      if (type === 'routes') {
        setSelectedItem(item);
        setSelectedItemType('routes');
        setShowEditModal(true);
      }
    },
    showEqualizer: (type, item) => {
      if (type === 'routes') {
        openEqualizerModal(item, type);
      }
    },
    showVNC: () => {}, // Not applicable for routes
    listenToSink: () => {}, // Not applicable for routes
    visualizeSink: () => {}, // Not applicable for routes
    toggleActiveSource: () => {} // Not applicable for routes
  };

  if (isLoading) {
    return <div>Loading routes...</div>;
  }

  if (error) {
    return <div>Error: {error}</div>;
  }

  return (
    <div className="routes-container">
      <h2>Routes</h2>
      <div className="action-buttons">
        <ActionButton onClick={handleAddRoute}>Add Route</ActionButton>
      </div>
      {routes.length === 0 ? (
        <div>No routes available</div>
      ) : (
        <RouteList
          routes={routes}
          starredRoutes={starredRoutes}
          actions={actions}
          sortConfig={sortConfig}
          onSort={onSort}
        />
      )}
      <ConfirmationModal
        isOpen={showDeleteModal}
        onClose={() => setShowDeleteModal(false)}
        onConfirm={handleDeleteRoute}
        message={`Are you sure you want to delete the route "${routeToDelete}"?`}
      />
    </div>
  );
};

export default Routes;

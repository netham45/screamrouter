/**
 * React component for displaying and managing routes.
 * This component handles fetching, sorting, starring, deleting, and editing routes.
 * It also provides functionality to add new routes and show the equalizer modal for a route.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React, { useState, useEffect } from 'react';
import RouteList from './RouteList';
import { useAppContext } from '../context/AppContext';
import { Actions } from '../utils/actions';
import ActionButton from './controls/ActionButton';
import { SortConfig, useAnchorFlash } from '../utils/commonUtils';
import ConfirmationModal from './ConfirmationModal';
import ApiService from '../api/api';

/**
 * React functional component for rendering the routes management page.
 *
 * @returns {JSX.Element} The rendered JSX element.
 */
const Routes: React.FC = () => {
  /**
   * Hook to flash anchor elements into view when they are focused.
   */
  useAnchorFlash();

  /**
   * Destructuring context values from AppContext.
   */
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
  
  /**
   * State to track loading status.
   */
  const [isLoading, setIsLoading] = useState(false);

  /**
   * State to track error messages.
   */
  const [error, setError] = useState<string | null>(null);

  /**
   * State to track starred routes.
   */
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);

  /**
   * State to track sorting configuration.
   */
  const [sortConfig, setSortConfig] = useState<SortConfig>({ key: 'name', direction: 'asc' });

  /**
   * State to control the visibility of the delete confirmation modal.
   */
  const [showDeleteModal, setShowDeleteModal] = useState(false);

  /**
   * State to track the route name that is being deleted.
   */
  const [routeToDelete, setRouteToDelete] = useState<string | null>(null);

  /**
   * Effect hook to load routes and starred routes on component mount.
   */
  useEffect(() => {
    if (routes.length === 0) {
      loadRoutes();
    }
    const savedStarredRoutes = localStorage.getItem('starredRoutes');
    if (savedStarredRoutes) {
      setStarredRoutes(JSON.parse(savedStarredRoutes));
    }
  }, []);

  /**
   * Function to fetch routes from the API.
   */
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

  /**
   * Function to handle sorting of routes.
   *
   * @param {string} key - The key by which to sort the routes.
   */
  const onSort = (key: string) => {
    setSortConfig(prevConfig => ({
      key,
      direction: prevConfig.key === key && prevConfig.direction === 'asc' ? 'desc' : 'asc',
    }));
  };

  /**
   * Function to toggle a route's starred status.
   *
   * @param {string} name - The name of the route to toggle.
   */
  const onToggleStar = async (name: string) => {
    setStarredRoutes(prev => {
      const newStarred = prev.includes(name)
        ? prev.filter(route => route !== name)
        : [...prev, name];
      localStorage.setItem('starredRoutes', JSON.stringify(newStarred));
      return newStarred;
    });
  };

  /**
   * Function to handle adding a new route.
   */
  const handleAddRoute = () => {
    setSelectedItem(null);
    setSelectedItemType('routes');
    setShowEditModal(true);
  };

  /**
   * Function to handle deleting a selected route.
   */
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

  /**
   * Actions object containing functions for managing routes.
   */
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
    toggleActiveSource: () => {}, // Not applicable for routes
  };

  /**
   * Renders the Routes component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
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

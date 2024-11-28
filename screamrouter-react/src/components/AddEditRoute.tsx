/**
 * This file contains the AddEditRoute component, which provides a modal interface for adding or editing routes.
 * It includes functionality for selecting sources and sinks, adjusting volume and timeshift, and saving changes.
 */

import React, { useState, useEffect } from 'react';
import ApiService, { Route, Source, Sink } from '../api/api';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import ActionButton from './controls/ActionButton';

/**
 * Interface defining the props for the AddEditRoute component.
 */
interface AddEditRouteProps {
  route?: Route;
  onClose: () => void;
  onSave: () => void;
}

/**
 * React functional component that provides a modal interface for adding or editing routes.
 * @param {AddEditRouteProps} props - Component properties including optional route data, close function, and save function.
 * @returns {JSX.Element} Rendered AddEditRoute component.
 */
const AddEditRoute: React.FC<AddEditRouteProps> = ({ route, onClose, onSave }) => {
  const [name, setName] = useState(route?.name || '');
  const [source, setSource] = useState(route?.source || '');
  const [sink, setSink] = useState(route?.sink || '');
  const [volume, setVolume] = useState(route?.volume || 1);
  const [timeshift, setTimeshift] = useState(route?.timeshift || 0);
  const [enabled] = useState(route?.enabled ?? true);
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [error, setError] = useState<string | null>(null);

  /**
   * Effect hook that fetches sources and sinks when the component mounts.
   */
  useEffect(() => {
    const fetchSourcesAndSinks = async () => {
      try {
        const [sourcesResponse, sinksResponse] = await Promise.all([
          ApiService.getSources(),
          ApiService.getSinks()
        ]);

        // Convert Record<string, Source> to Source[]
        const sourcesArray = Object.values(sourcesResponse.data);
        // Convert Record<string, Sink> to Sink[]
        const sinksArray = Object.values(sinksResponse.data);

        setSources(sourcesArray);
        setSinks(sinksArray);
      } catch (error) {
        console.error('Error fetching sources and sinks:', error);
        setError('Failed to fetch sources and sinks. Please try again.');
      }
    };

    fetchSourcesAndSinks();
  }, []);

  /**
   * Handles form submission for adding or updating a route.
   */
  const handleSubmit = async () => {
    const routeData: Partial<Route> = {
      name,
      source,
      sink,
      volume,
      timeshift,
      enabled,
    };

    try {
      if (route) {
        await ApiService.updateRoute(route.name, routeData);
      } else {
        await ApiService.addRoute(routeData as Route);
      }
      onSave();
      onClose();
    } catch (error) {
      console.error('Error submitting route:', error);
      setError('Failed to submit route. Please try again.');
    }
  };

  /**
   * Main render method for the AddEditRoute component.
   * @returns {JSX.Element} Rendered AddEditRoute component structure.
   */
  return (
    <div className="modal-backdrop">
      <div className="modal-content">
        <div className="add-edit-route">
          <h3>{route ? 'Edit Route' : 'Add Route'}</h3>
          {error && <div className="error-message">{error}</div>}
          <div className="route-form">
            <label>
              Route Name:
              <input 
                type="text" 
                value={name} 
                onChange={(e) => setName(e.target.value)} 
                required 
              />
            </label>
            <label>
              Source:
              <select 
                value={source} 
                onChange={(e) => setSource(e.target.value)}
                required
              >
                <option value="">Select a source</option>
                {sources.map((src) => (
                  <option key={src.name} value={src.name}>{src.name}</option>
                ))}
              </select>
            </label>
            <label>
              Sink:
              <select 
                value={sink} 
                onChange={(e) => setSink(e.target.value)}
                required
              >
                <option value="">Select a sink</option>
                {sinks.map((snk) => (
                  <option key={snk.name} value={snk.name}>{snk.name}</option>
                ))}
              </select>
            </label>
            <VolumeSlider value={volume} onChange={setVolume} />
            <TimeshiftSlider value={timeshift} onChange={setTimeshift} />
            <div className="form-buttons">
              <ActionButton onClick={handleSubmit}>
                {route ? 'Update Route' : 'Add Route'}
              </ActionButton>
              <ActionButton onClick={onClose}>Cancel</ActionButton>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

/**
 * Exports the AddEditRoute component as the default export.
 */
export default AddEditRoute;

import React, { useState, useEffect } from 'react';
import ApiService, { Route, Source, Sink } from '../api/api';

/**
 * Props for the AddEditRoute component
 * @interface AddEditRouteProps
 * @property {Route} [route] - The route to edit (undefined for adding a new route)
 * @property {() => void} onClose - Function to call when closing the form
 * @property {() => void} onSubmit - Function to call after successful submission
 */
interface AddEditRouteProps {
  route?: Route;
  onClose: () => void;
  onSubmit: () => void;
}

/**
 * AddEditRoute component for adding or editing a route
 * @param {AddEditRouteProps} props - The component props
 * @returns {React.FC} A functional component for adding or editing routes
 */
const AddEditRoute: React.FC<AddEditRouteProps> = ({ route, onClose, onSubmit }) => {
  // State declarations
  const [name, setName] = useState(route?.name || '');
  const [source, setSource] = useState(route?.source || '');
  const [sink, setSink] = useState(route?.sink || '');
  const [delay, setDelay] = useState(route?.delay?.toString() || '0');
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [error, setError] = useState<string | null>(null);

  /**
   * Fetches sources and sinks data from the API
   */
  useEffect(() => {
    const fetchData = async () => {
      try {
        const [sourcesResponse, sinksResponse] = await Promise.all([
          ApiService.getSources(),
          ApiService.getSinks()
        ]);
        setSources(sourcesResponse.data);
        setSinks(sinksResponse.data);
      } catch (error) {
        console.error('Error fetching sources and sinks:', error);
        setError('Failed to fetch sources and sinks. Please try again.');
      }
    };
    fetchData();
  }, []);

  /**
   * Handles form submission
   * @param {React.FormEvent} e - The form event
   */
  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    const routeData: Partial<Route> = {
      name,
      source,
      sink,
      delay: parseInt(delay),
    };

    try {
      if (route) {
        await ApiService.updateRoute(route.name, routeData);
      } else {
        await ApiService.addRoute(routeData as Route);
      }
      onSubmit();
      onClose();
    } catch (error) {
      console.error('Error submitting route:', error);
      setError('Failed to submit route. Please try again.');
    }
  };

  return (
    <div className="add-edit-route">
      <h3>{route ? 'Edit Route' : 'Add Route'}</h3>
      {error && <div className="error-message">{error}</div>}
      <form onSubmit={handleSubmit}>
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
        <label>
          Delay (ms):
          <input 
            type="number" 
            value={delay} 
            onChange={(e) => setDelay(e.target.value)} 
            min="0" 
            max="5000" 
          />
        </label>
        <div className="form-buttons">
          <button type="submit">{route ? 'Update Route' : 'Add Route'}</button>
          <button type="button" onClick={onClose}>Cancel</button>
        </div>
      </form>
    </div>
  );
};

export default AddEditRoute;
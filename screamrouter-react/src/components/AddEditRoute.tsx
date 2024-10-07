import React, { useState, useEffect } from 'react';
import ApiService, { Route, Source, Sink } from '../api/api';

interface AddEditRouteProps {
  route?: Route;
  onClose: () => void;
  onSubmit: () => void;
}

const AddEditRoute: React.FC<AddEditRouteProps> = ({ route, onClose, onSubmit }) => {
  const [name, setName] = useState(route?.name || '');
  const [source, setSource] = useState(route?.source || '');
  const [sink, setSink] = useState(route?.sink || '');
  const [delay, setDelay] = useState(route?.delay?.toString() || '0');
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);

  useEffect(() => {
    const fetchData = async () => {
      try {
        const sourcesResponse = await ApiService.getSources();
        const sinksResponse = await ApiService.getSinks();
        setSources(sourcesResponse.data);
        setSinks(sinksResponse.data);
      } catch (error) {
        console.error('Error fetching sources and sinks:', error);
      }
    };
    fetchData();
  }, []);

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
    }
  };

  return (
    <div className="add-edit-route">
      <h3>{route ? 'Edit Route' : 'Add Route'}</h3>
      <form onSubmit={handleSubmit}>
        <label>
          Route Name:
          <input type="text" value={name} onChange={(e) => setName(e.target.value)} required />
        </label>
        <label>
          Source:
          <select value={source} onChange={(e) => setSource(e.target.value)} required>
            <option value="">Select a source</option>
            {sources.map((src) => (
              <option key={src.name} value={src.name}>{src.name}</option>
            ))}
          </select>
        </label>
        <label>
          Sink:
          <select value={sink} onChange={(e) => setSink(e.target.value)} required>
            <option value="">Select a sink</option>
            {sinks.map((snk) => (
              <option key={snk.name} value={snk.name}>{snk.name}</option>
            ))}
          </select>
        </label>
        <label>
          Delay (ms):
          <input type="number" value={delay} onChange={(e) => setDelay(e.target.value)} min="0" max="5000" />
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
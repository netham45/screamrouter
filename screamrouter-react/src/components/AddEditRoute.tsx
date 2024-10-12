import React, { useState, useEffect } from 'react';
import ApiService, { Route, Source, Sink } from '../api/api';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import EnableButton from './controls/EnableButton';
import ActionButton from './controls/ActionButton';

interface AddEditRouteProps {
  route?: Route;
  onClose: () => void;
  onSave: () => void;
}

const AddEditRoute: React.FC<AddEditRouteProps> = ({ route, onClose, onSave }) => {
  const [name, setName] = useState(route?.name || '');
  const [source, setSource] = useState(route?.source || '');
  const [sink, setSink] = useState(route?.sink || '');
  const [volume, setVolume] = useState(route?.volume || 1);
  const [timeshift, setTimeshift] = useState(route?.timeshift || 0);
  const [enabled, setEnabled] = useState(route?.enabled ?? true);
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchSourcesAndSinks = async () => {
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

    fetchSourcesAndSinks();
  }, []);

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

export default AddEditRoute;
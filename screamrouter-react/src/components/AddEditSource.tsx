/**
 * React component for adding or editing a source.
 * This component provides a form to input details about a source, including its name, IP address,
 * VNC IP and port, volume, and delay. It allows the user to either add a new source or update an existing one.
 *
 * @param {AddEditSourceProps} props - The properties for the component.
 * @param {Source | undefined} props.source - Optional. If provided, this is the source being edited.
 * @param {() => void} props.onClose - Callback function to close the modal.
 * @param {() => void} props.onSave - Callback function to save changes and close the modal.
 */
import React, { useState } from 'react';
import ApiService, { Source } from '../api/api';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

interface AddEditSourceProps {
  source?: Source;
  onClose: () => void;
  onSave: () => void;
}

const AddEditSource: React.FC<AddEditSourceProps> = ({ source, onClose, onSave }) => {
  const [name, setName] = useState(source?.name || '');
  const [ip, setIp] = useState(source?.ip || '');
  const [vncIp, setVncIp] = useState(source?.vnc_ip || '');
  const [vncPort, setVncPort] = useState(source?.vnc_port?.toString() || '');
  const [volume, setVolume] = useState(source?.volume || 1);
  const [delay, setDelay] = useState(source?.delay || 0);
  const [error, setError] = useState<string | null>(null);

  /**
   * Handles form submission to add or update a source.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    const sourceData: Partial<Source> = {
      name,
      ip,
      vnc_ip: vncIp,
      vnc_port: vncPort ? parseInt(vncPort) : undefined,
      volume,
      delay,
    };

    try {
      if (source) {
        await ApiService.updateSource(source.name, sourceData);
      } else {
        await ApiService.addSource(sourceData as Source);
      }
      onSave();
      onClose();
    } catch (error) {
      console.error('Error submitting source:', error);
      setError('Failed to submit source. Please try again.');
    }
  };

  return (
    <div className="modal-backdrop">
      <div className="modal-content">
        <div className="add-edit-source">
          <h3>{source ? 'Edit Source' : 'Add Source'}</h3>
          {error && <div className="error-message">{error}</div>}
          <div className="source-form">
            <label>
              Source Name:
              <input 
                type="text" 
                value={name} 
                onChange={(e) => setName(e.target.value)} 
                required 
              />
            </label>
            <label>
              Source IP:
              <input 
                type="text" 
                value={ip} 
                onChange={(e) => setIp(e.target.value)} 
                required 
              />
            </label>
            <label>
              VNC IP:
              <input 
                type="text" 
                value={vncIp} 
                onChange={(e) => setVncIp(e.target.value)} 
              />
            </label>
            <label>
              VNC Port:
              <input 
                type="number" 
                value={vncPort} 
                onChange={(e) => setVncPort(e.target.value)} 
              />
            </label>
            <VolumeSlider value={volume} onChange={setVolume} />
            <TimeshiftSlider value={delay} onChange={setDelay} />
            <div className="form-buttons">
              <ActionButton onClick={handleSubmit}>
                {source ? 'Update Source' : 'Add Source'}
              </ActionButton>
              <ActionButton onClick={onClose}>Cancel</ActionButton>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default AddEditSource;

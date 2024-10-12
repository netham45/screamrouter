import React, { useState } from 'react';
import ApiService, { Source } from '../api/api';

/**
 * Props for the AddEditSource component
 * @interface AddEditSourceProps
 * @property {Source} [source] - The source to edit (undefined for adding a new source)
 * @property {() => void} onClose - Function to call when closing the form
 * @property {() => void} onSubmit - Function to call after successful submission
 */
interface AddEditSourceProps {
  source?: Source;
  onClose: () => void;
  onSubmit: () => void;
}

/**
 * AddEditSource component for adding or editing a source
 * @param {AddEditSourceProps} props - The component props
 * @returns {React.FC} A functional component for adding or editing sources
 */
const AddEditSource: React.FC<AddEditSourceProps> = ({ source, onClose, onSubmit }) => {
  // State declarations
  const [name, setName] = useState(source?.name || '');
  const [ip, setIp] = useState(source?.ip || '');
  const [vncIp, setVncIp] = useState(source?.vnc_ip || '');
  const [vncPort, setVncPort] = useState(source?.vnc_port?.toString() || '');
  const [delay, setDelay] = useState(source?.delay?.toString() || '0');
  const [error, setError] = useState<string | null>(null);

  /**
   * Handles form submission
   * @param {React.FormEvent} e - The form event
   */
  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    const sourceData: Partial<Source> = {
      name,
      ip,
      vnc_ip: vncIp,
      vnc_port: vncPort ? parseInt(vncPort) : undefined,
      delay: parseInt(delay),
    };

    try {
      if (source) {
        await ApiService.updateSource(source.name, sourceData);
      } else {
        await ApiService.addSource(sourceData as Source);
      }
      onSubmit();
      onClose();
    } catch (error) {
      console.error('Error submitting source:', error);
      setError('Failed to submit source. Please try again.');
    }
  };

  return (
    <div className="add-edit-source">
      <h3>{source ? 'Edit Source' : 'Add Source'}</h3>
      {error && <div className="error-message">{error}</div>}
      <form onSubmit={handleSubmit}>
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
          <button type="submit">{source ? 'Update Source' : 'Add Source'}</button>
          <button type="button" onClick={onClose}>Cancel</button>
        </div>
      </form>
    </div>
  );
};

export default AddEditSource;
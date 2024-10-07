import React, { useState } from 'react';
import ApiService, { Source } from '../api/api';

interface AddEditSourceProps {
  source?: Source;
  onClose: () => void;
  onSubmit: () => void;
}

const AddEditSource: React.FC<AddEditSourceProps> = ({ source, onClose, onSubmit }) => {
  const [name, setName] = useState(source?.name || '');
  const [ip, setIp] = useState(source?.ip || '');
  const [vncIp, setVncIp] = useState(source?.vnc_ip || '');
  const [vncPort, setVncPort] = useState(source?.vnc_port?.toString() || '');
  const [delay, setDelay] = useState(source?.delay?.toString() || '0');

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    const sourceData: Partial<Source> = {
      name,
      ip,
      vnc_ip: vncIp,
      vnc_port: parseInt(vncPort),
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
    }
  };

  return (
    <div className="add-edit-source">
      <h3>{source ? 'Edit Source' : 'Add Source'}</h3>
      <form onSubmit={handleSubmit}>
        <label>
          Source Name:
          <input type="text" value={name} onChange={(e) => setName(e.target.value)} required />
        </label>
        <label>
          Source IP:
          <input type="text" value={ip} onChange={(e) => setIp(e.target.value)} required />
        </label>
        <label>
          VNC IP:
          <input type="text" value={vncIp} onChange={(e) => setVncIp(e.target.value)} />
        </label>
        <label>
          VNC Port:
          <input type="number" value={vncPort} onChange={(e) => setVncPort(e.target.value)} />
        </label>
        <label>
          Delay (ms):
          <input type="number" value={delay} onChange={(e) => setDelay(e.target.value)} min="0" max="5000" />
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
/**
 * React component for adding or editing a sink.
 * This component provides a form to input details about a sink, including its name, IP address,
 * port, bit depth, sample rate, channels, channel layout, volume, delay, time sync settings, and more.
 * It allows the user to either add a new sink or update an existing one.
 *
 * @param {AddEditSinkProps} props - The properties for the component.
 * @param {Sink | undefined} props.sink - Optional. If provided, this is the sink being edited.
 * @param {() => void} props.onClose - Callback function to close the modal.
 * @param {() => void} props.onSave - Callback function to save changes and close the modal.
 */
import React, { useState } from 'react';
import ApiService, { Sink } from '../api/api';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

interface AddEditSinkProps {
  sink?: Sink;
  onClose: () => void;
  onSave: () => void;
}

const AddEditSink: React.FC<AddEditSinkProps> = ({ sink, onClose, onSave }) => {
  const [name, setName] = useState(sink?.name || '');
  const [ip, setIp] = useState(sink?.ip || '');
  const [port, setPort] = useState(sink?.port?.toString() || '4010');
  const [bitDepth, setBitDepth] = useState(sink?.bit_depth?.toString() || '32');
  const [sampleRate, setSampleRate] = useState(sink?.sample_rate?.toString() || '48000');
  const [channels, setChannels] = useState(sink?.channels?.toString() || '2');
  const [channelLayout, setChannelLayout] = useState(sink?.channel_layout || 'stereo');
  const [volume, setVolume] = useState(sink?.volume || 1);
  const [delay, setDelay] = useState(sink?.delay || 0);
  const [timeSync, setTimeSync] = useState(sink?.time_sync || false);
  const [timeSyncDelay, setTimeSyncDelay] = useState(sink?.time_sync_delay?.toString() || '0');
  const [error, setError] = useState<string | null>(null);

  /**
   * Handles form submission to add or update a sink.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    const sinkData: Partial<Sink> = {
      name,
      ip,
      port: parseInt(port),
      bit_depth: parseInt(bitDepth),
      sample_rate: parseInt(sampleRate),
      channels: parseInt(channels),
      channel_layout: channelLayout,
      volume,
      delay,
      time_sync: timeSync,
      time_sync_delay: parseInt(timeSyncDelay),
    };

    try {
      if (sink) {
        await ApiService.updateSink(sink.name, sinkData);
      } else {
        await ApiService.addSink(sinkData as Sink);
      }
      onSave();
      onClose();
    } catch (error) {
      console.error('Error submitting sink:', error);
      setError('Failed to submit sink. Please try again.');
    }
  };

  return (
    <div className="modal-backdrop">
      <div className="modal-content">
        <div className="add-edit-sink">
          <h3>{sink ? 'Edit Sink' : 'Add Sink'}</h3>
          {error && <div className="error-message">{error}</div>}
          <div className="sink-form">
            <label>
              Sink Name:
              <input 
                type="text" 
                value={name} 
                onChange={(e) => setName(e.target.value)} 
                required 
              />
            </label>
            <label>
              Sink IP:
              <input 
                type="text" 
                value={ip} 
                onChange={(e) => setIp(e.target.value)} 
                required 
              />
            </label>
            <label>
              Sink Port:
              <input 
                type="number" 
                value={port} 
                onChange={(e) => setPort(e.target.value)} 
                min="1" 
                max="65535" 
                required 
              />
            </label>
            <label>
              Bit Depth:
              <select value={bitDepth} onChange={(e) => setBitDepth(e.target.value)}>
                <option value="16">16</option>
                <option value="24">24</option>
                <option value="32">32</option>
              </select>
            </label>
            <label>
              Sample Rate:
              <select value={sampleRate} onChange={(e) => setSampleRate(e.target.value)}>
                <option value="44100">44100</option>
                <option value="48000">48000</option>
                <option value="88200">88200</option>
                <option value="96000">96000</option>
                <option value="192000">192000</option>
              </select>
            </label>
            <label>
              Channels:
              <input 
                type="number" 
                value={channels} 
                onChange={(e) => setChannels(e.target.value)} 
                min="1" 
                max="8" 
                required 
              />
            </label>
            <label>
              Channel Layout:
              <select value={channelLayout} onChange={(e) => setChannelLayout(e.target.value)}>
                <option value="mono">Mono</option>
                <option value="stereo">Stereo</option>
                <option value="quad">Quad</option>
                <option value="surround">Surround</option>
                <option value="5.1">5.1</option>
                <option value="7.1">7.1</option>
              </select>
            </label>
            <VolumeSlider value={volume} onChange={setVolume} />
            <TimeshiftSlider value={delay} onChange={setDelay} />
            <label>
              Time Sync:
              <input 
                type="checkbox" 
                checked={timeSync} 
                onChange={(e) => setTimeSync(e.target.checked)} 
              />
            </label>
            <label>
              Time Sync Delay (ms):
              <input 
                type="number" 
                value={timeSyncDelay} 
                onChange={(e) => setTimeSyncDelay(e.target.value)} 
                min="0" 
                max="5000" 
              />
            </label>
            <div className="form-buttons">
              <ActionButton onClick={handleSubmit}>
                {sink ? 'Update Sink' : 'Add Sink'}
              </ActionButton>
              <ActionButton onClick={onClose}>Cancel</ActionButton>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default AddEditSink;

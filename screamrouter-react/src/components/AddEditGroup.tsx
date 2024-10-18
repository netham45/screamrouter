import React, { useState, useEffect } from 'react';
import ApiService, { Source, Sink } from '../api/api';
import ActionButton from './controls/ActionButton';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';

interface AddEditGroupProps {
  type: 'source' | 'sink';
  group?: Source | Sink;
  onClose: () => void;
  onSave: () => void;
}

interface GroupData {
  name: string;
  group_members: string[];
  volume: number;
  delay: number;
  is_group: boolean;
  enabled: boolean;
  equalizer: {
    b1: number; b2: number; b3: number; b4: number; b5: number; b6: number; b7: number; b8: number;
    b9: number; b10: number; b11: number; b12: number; b13: number; b14: number; b15: number; b16: number; b17: number; b18: number;
  };
  vnc_ip?: string;
  vnc_port?: number;
}

const AddEditGroup: React.FC<AddEditGroupProps> = ({ type, group, onClose, onSave }) => {
  const [name, setName] = useState(group ? group.name : '');
  const [members, setMembers] = useState<string[]>(group ? group.group_members || [] : []);
  const [volume, setVolume] = useState(group ? group.volume : 1);
  const [delay, setDelay] = useState(group ? group.delay : 0);
  const [vncIp, setVncIp] = useState(group && 'vnc_ip' in group ? group.vnc_ip : '');
  const [vncPort, setVncPort] = useState(group && 'vnc_port' in group ? group.vnc_port : 0);
  const [availableMembers, setAvailableMembers] = useState<(Source | Sink)[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchAvailableMembers = async () => {
      try {
        const response = type === 'source'
          ? await ApiService.getSources() 
          : await ApiService.getSinks();
        
        const filteredMembers = (response.data as (Source | Sink)[]).filter(
          (item): item is Source | Sink => !('is_group' in item) || !item.is_group
        );
        setAvailableMembers(filteredMembers);
      } catch (error) {
        console.error(`Error fetching ${type}s:`, error);
        setError(`Failed to fetch ${type}s. Please try again.`);
      }
    };

    fetchAvailableMembers();
  }, [type]);

  const handleSubmit = async () => {
    const groupData: GroupData = {
      name,
      group_members: members,
      volume,
      delay,
      is_group: true,
      enabled: true,
      equalizer: {
        b1: 0, b2: 0, b3: 0, b4: 0, b5: 0, b6: 0, b7: 0, b8: 0,
        b9: 0, b10: 0, b11: 0, b12: 0, b13: 0, b14: 0, b15: 0, b16: 0, b17: 0, b18: 0
      },
    };

    if (type === 'source') {
      groupData.vnc_ip = vncIp;
      groupData.vnc_port = vncPort;
    }

    try {
      if (group) {
        if (type === 'source') {
          await ApiService.updateSource(name, {
            ...groupData,
            vnc_port: vncPort
          } as Partial<Source>);
        } else {
          await ApiService.updateSink(name, groupData as Partial<Sink>);
        }
      } else {
        if (type === 'source') {
          await ApiService.addSource({
            ...groupData,
            vnc_port: vncPort
          } as Source);
        } else {
          await ApiService.addSink({ ...groupData, port: 0 } as Sink);
        }
      }
      onSave();
      onClose();
    } catch (error) {
      console.error('Error saving group:', error);
      setError('Failed to save group. Please try again.');
    }
  };

  const toggleMember = (memberName: string) => {
    setMembers(prevMembers =>
      prevMembers.includes(memberName)
        ? prevMembers.filter(m => m !== memberName)
        : [...prevMembers, memberName]
    );
  };

  return (
    <div className="modal-backdrop">
      <div className="modal-content">
        <div className="add-edit-group">
          <h3>{group ? 'Edit' : 'Add'} {type.charAt(0).toUpperCase() + type.slice(1)} Group</h3>
          {error && <div className="error-message">{error}</div>}
          <div className="group-form">
            <label>
              Group Name:
              <input
                type="text"
                value={name}
                onChange={(e) => setName(e.target.value)}
                required
              />
            </label>
            <VolumeSlider value={volume} onChange={setVolume} />
            <TimeshiftSlider value={delay} onChange={setDelay} />
            {type === 'source' && (
              <>
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
                    type="text"
                    value={vncPort}
                    onChange={(e) => setVncPort(parseInt(e.target.value, 10))}
                  />
                </label>
              </>
            )}
            <div>
              <label>Members:</label>
              {availableMembers.map((member) => (
                <div key={member.name}>
                  <input
                    type="checkbox"
                    id={member.name}
                    checked={members.includes(member.name)}
                    onChange={() => toggleMember(member.name)}
                  />
                  <label htmlFor={member.name}>{member.name}</label>
                </div>
              ))}
            </div>
            <div className="form-buttons">
              <ActionButton onClick={handleSubmit}>
                {group ? 'Update Group' : 'Add Group'}
              </ActionButton>
              <ActionButton onClick={onClose}>Cancel</ActionButton>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default AddEditGroup;

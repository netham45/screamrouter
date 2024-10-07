import React, { useState, useEffect } from 'react';
import ApiService, { Source, Sink } from '../api/api';

interface AddEditGroupProps {
  type: 'source' | 'sink';
  group?: Source | Sink;
  onClose: () => void;
  onSubmit: () => void;
}

const AddEditGroup: React.FC<AddEditGroupProps> = ({ type, group, onClose, onSubmit }) => {
  const [name, setName] = useState(group ? group.name : '');
  const [members, setMembers] = useState<string[]>(group ? group.group_members || [] : []);
  const [delay, setDelay] = useState(group ? group.delay : 0);
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
      }
    };

    fetchAvailableMembers();
  }, [type]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);

    if (!name) {
      setError('Name is required');
      return;
    }

    if (members.length === 0) {
      setError('At least one member is required');
      return;
    }

    try {
      const groupData = {
        name,
        group_members: members,
        delay,
        is_group: true,
        enabled: true,
        volume: 1,
        equalizer: {
          b1: 0, b2: 0, b3: 0, b4: 0, b5: 0, b6: 0, b7: 0, b8: 0,
          b9: 0, b10: 0, b11: 0, b12: 0, b13: 0, b14: 0, b15: 0, b16: 0, b17: 0, b18: 0
        },
      };

      if (group) {
        if (type === 'source') {
          await ApiService.updateSource(name, groupData);
        } else {
          await ApiService.updateSink(name, groupData);
        }
      } else {
        if (type === 'source') {
          await ApiService.addSource(groupData as Source);
        } else {
          await ApiService.addSink({ ...groupData, port: 0 } as Sink);
        }
      }
      onSubmit();
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
    <div className="add-edit-group">
      <h2>{group ? 'Edit' : 'Add'} {type.charAt(0).toUpperCase() + type.slice(1)} Group</h2>
      {error && <div className="error-message">{error}</div>}
      <form onSubmit={handleSubmit}>
        <div>
          <label htmlFor="name">Name:</label>
          <input
            type="text"
            id="name"
            value={name}
            onChange={(e) => setName(e.target.value)}
            required
          />
        </div>
        <div>
          <label htmlFor="delay">Delay (ms):</label>
          <input
            type="number"
            id="delay"
            value={delay}
            onChange={(e) => setDelay(parseInt(e.target.value))}
            min="0"
            required
          />
        </div>
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
        <div className="form-actions">
          <button type="submit">Save</button>
          <button type="button" onClick={onClose}>Cancel</button>
        </div>
      </form>
    </div>
  );
};

export default AddEditGroup;
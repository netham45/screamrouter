import React from 'react';
import { ActionButton } from '../utils/commonUtils';

interface ActiveSourceProps {
  activeSource: string | null;
  onToggleActiveSource: (sourceName: string) => void;
}

const ActiveSource: React.FC<ActiveSourceProps> = ({ activeSource, onToggleActiveSource }) => {
  if (!activeSource) {
    return (
      <div className="active-source">
        <h3>Active Source</h3>
        <p>No active source</p>
      </div>
    );
  }

  return (
    <div className="active-source">
      <h3>Active Source</h3>
      <div className="active-source-content">
        <span>{activeSource}</span>
        <ActionButton onClick={() => onToggleActiveSource(activeSource)}>
          Deactivate
        </ActionButton>
      </div>
    </div>
  );
};

export default ActiveSource;
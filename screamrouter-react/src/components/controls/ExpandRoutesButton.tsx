import React, { useState } from 'react';
import { Route } from '../../api/api';
import ActionButton from './ActionButton';

interface ExpandRoutesButtonProps {
  activeRoutes: Route[];
  disabledRoutes: Route[];
}

const ExpandRoutesButton: React.FC<ExpandRoutesButtonProps> = ({ activeRoutes, disabledRoutes }) => {
  const [isExpanded, setIsExpanded] = useState(false);

  const toggleExpand = () => {
    setIsExpanded(!isExpanded);
  };

  return (
    <div className="expand-routes-button">
      <ActionButton onClick={toggleExpand}>
        {isExpanded ? 'Hide Routes' : 'Show Routes'}
      </ActionButton>
      {isExpanded && (
        <div className="routes-list">
          <h4>Active Routes:</h4>
          <ul>
            {activeRoutes.map(route => (
              <li key={route.name}>{route.name}</li>
            ))}
          </ul>
          <h4>Disabled Routes:</h4>
          <ul>
            {disabledRoutes.map(route => (
              <li key={route.name}>{route.name}</li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
};

export default ExpandRoutesButton;

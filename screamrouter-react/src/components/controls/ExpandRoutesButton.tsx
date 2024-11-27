/**
 * React component for rendering a button that expands and collapses a list of routes.
 * It includes both active and disabled routes and can navigate to a specific route if used in the desktop menu.
 */
import React, { useState } from 'react';
import { Route } from '../../api/api';
import ActionButton from './ActionButton';

/**
 * Interface defining the props for the ExpandRoutesButton component.
 */
interface ExpandRoutesButtonProps {
  /**
   * Array of active routes.
   */
  activeRoutes: Route[];
  /**
   * Array of disabled routes.
   */
  disabledRoutes: Route[];
  /**
   * Optional boolean indicating if the button is used in the desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Optional callback function to navigate to a specific route when clicked.
   */
  onNavigate?: (type: 'routes', itemName: string) => void;
}

/**
 * React functional component for rendering an expand/collapse button for routes.
 *
 * @param {ExpandRoutesButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ExpandRoutesButton: React.FC<ExpandRoutesButtonProps> = ({ activeRoutes, disabledRoutes, isDesktopMenu, onNavigate }) => {
  /**
   * State variable indicating whether the routes list is expanded or collapsed.
   */
  const [isExpanded, setIsExpanded] = useState(false);

  /**
   * Function to toggle the expansion state of the routes list.
   */
  const toggleExpand = () => {
    setIsExpanded(!isExpanded);
  };

  /**
   * Function to handle clicking on a route name.
   *
   * @param {string} routeName - The name of the clicked route.
   */
  const handleRouteClick = (routeName: string) => {
    if (isDesktopMenu && onNavigate) {
      onNavigate('routes', routeName);
    }
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
              <li key={route.name} onClick={() => handleRouteClick(route.name)} style={isDesktopMenu ? { cursor: 'pointer' } : {}}>
                {route.name}
              </li>
            ))}
          </ul>
          <h4>Disabled Routes:</h4>
          <ul>
            {disabledRoutes.map(route => (
              <li key={route.name} onClick={() => handleRouteClick(route.name)} style={isDesktopMenu ? { cursor: 'pointer' } : {}}>
                {route.name}
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  );
};

export default ExpandRoutesButton;

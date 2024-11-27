/**
 * React component for rendering a list of routes (both active and disabled) associated with an item.
 * It includes functionality to expand and collapse the route lists and navigate to specific routes if used in the desktop menu.
 */
import React from 'react';
import { Route } from '../../api/api';
import { renderLinkWithAnchor, ActionButton } from '../../utils/commonUtils';

/**
 * Interface defining the props for the ItemRoutes component.
 */
interface ItemRoutesProps {
  /**
   * Array of active routes associated with the item.
   */
  activeRoutes: Route[];
  /**
   * Array of disabled routes associated with the item.
   */
  disabledRoutes: Route[];
  /**
   * Boolean indicating whether the route lists are expanded or collapsed.
   */
  isExpanded: boolean;
  /**
   * Callback function to toggle the expansion state of the route lists.
   */
  toggleExpandRoutes: (name: string) => void;
  /**
   * Name of the item associated with these routes.
   */
  itemName: string;
  /**
   * Optional boolean indicating if the component is used in the desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Optional callback function to navigate to a specific route when clicked.
   */
  onNavigate?: (type: 'routes', name: string) => void;
}

/**
 * React functional component for rendering routes associated with an item.
 *
 * @param {ItemRoutesProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ItemRoutes: React.FC<ItemRoutesProps> = ({
  activeRoutes,
  disabledRoutes,
  isExpanded,
  toggleExpandRoutes,
  itemName,
  isDesktopMenu = false,
  onNavigate
}) => {
  /**
   * Logs the props received by the component for debugging purposes.
   */
  console.log('ItemRoutes props:', { activeRoutes, disabledRoutes, isExpanded, itemName, isDesktopMenu });

  /**
   * Function to render a list of routes.
   *
   * @param {Route[]} routes - The array of routes to render.
   * @param {boolean} isEnabled - Boolean indicating if the routes are enabled or disabled.
   * @returns {JSX.Element | null} The rendered JSX element for the route list, or null if no routes are provided.
   */
  const renderRouteList = (routes: Route[], isEnabled: boolean) => {
    /**
     * Logs the parameters received by the function for debugging purposes.
     */
    console.log('renderRouteList called with:', { routes, isEnabled });

    /**
     * Checks if the routes parameter is an array and logs an error if it is not.
     */
    if (!Array.isArray(routes)) {
      console.error('routes is not an array:', routes);
      return null;
    }

    /**
     * Returns null if there are no routes to display.
     */
    if (routes.length === 0) return null;

    /**
     * Determines the routes to display based on whether the list is expanded or not.
     */
    const displayedRoutes = isExpanded ? routes : routes.slice(0, 3);
    /**
     * Boolean indicating if there are more routes than those currently displayed.
     */
    const hasMore = routes.length > 3;

    return (
      <div className={`route-list ${isEnabled ? 'enabled' : 'disabled'}`}>
        <span className="route-list-label">{isEnabled ? 'Enabled routes:' : 'Disabled routes:'}</span>
        {displayedRoutes.map((route, index) => (
          <React.Fragment key={route.name}>
            {isDesktopMenu ? (
              <span
                onClick={() => onNavigate && onNavigate('routes', route.name)}
                style={{ cursor: 'pointer', textDecoration: 'underline' }}
              >
                {route.name}
              </span>
            ) : (
              renderLinkWithAnchor('/routes', route.name, 'fa-route')
            )}
            {index < displayedRoutes.length - 1 && ', '}
          </React.Fragment>
        ))}
        {hasMore && !isExpanded && (
          <ActionButton onClick={() => toggleExpandRoutes(itemName)} className="expand-routes">
            ...
          </ActionButton>
        )}
      </div>
    );
  };

  return (
    <div className="item-routes">
      {renderRouteList(activeRoutes, true)}
      {renderRouteList(disabledRoutes, false)}
      {isExpanded && (
        <ActionButton onClick={() => toggleExpandRoutes(itemName)} className="collapse-routes">
          Show less
        </ActionButton>
      )}
    </div>
  );
};

export default ItemRoutes;

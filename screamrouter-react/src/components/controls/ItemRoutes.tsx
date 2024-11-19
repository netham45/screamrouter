import React from 'react';
import { Route } from '../../api/api';
import { renderLinkWithAnchor, ActionButton } from '../../utils/commonUtils';

interface ItemRoutesProps {
  activeRoutes: Route[];
  disabledRoutes: Route[];
  isExpanded: boolean;
  toggleExpandRoutes: (name: string) => void;
  itemName: string;
  isDesktopMenu?: boolean;
  onNavigate?: (type: 'routes', name: string) => void;
}

const ItemRoutes: React.FC<ItemRoutesProps> = ({
  activeRoutes,
  disabledRoutes,
  isExpanded,
  toggleExpandRoutes,
  itemName,
  isDesktopMenu = false,
  onNavigate
}) => {
  console.log('ItemRoutes props:', { activeRoutes, disabledRoutes, isExpanded, itemName, isDesktopMenu });

  const renderRouteList = (routes: Route[], isEnabled: boolean) => {
    console.log('renderRouteList called with:', { routes, isEnabled });

    if (!Array.isArray(routes)) {
      console.error('routes is not an array:', routes);
      return null;
    }

    if (routes.length === 0) return null;

    const displayedRoutes = isExpanded ? routes : routes.slice(0, 3);
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
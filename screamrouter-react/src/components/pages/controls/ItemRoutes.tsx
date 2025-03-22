/**
 * React component for rendering a list of routes (both active and disabled) associated with an item.
 * It includes functionality to expand and collapse the route lists and navigate to specific routes if used in the desktop menu.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  Box,
  Text,
  Flex,
  useColorModeValue
} from '@chakra-ui/react';
import { Route } from '../../../api/api';
import { renderLinkWithAnchor } from '../../../utils/commonUtils';
import ActionButton from './ActionButton';

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
  // Color values for light/dark mode
  const labelColor = useColorModeValue('gray.700', 'gray.300');
  const linkColor = useColorModeValue('blue.600', 'blue.300');

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
      <Box
        mb={2}
        opacity={isEnabled ? 1 : 0.7}
      >
        <Text
          fontWeight="medium"
          color={labelColor}
          mb={1}
        >
          {isEnabled ? 'Enabled routes:' : 'Disabled routes:'}
        </Text>
        <Flex wrap="wrap" alignItems="center">
          {displayedRoutes.map((route, index) => (
            <React.Fragment key={route.name}>
              {isDesktopMenu ? (
                <Text
                  onClick={() => onNavigate && onNavigate('routes', route.name)}
                  cursor="pointer"
                  textDecoration="underline"
                  color={linkColor}
                  mr={index < displayedRoutes.length - 1 ? 1 : 0}
                >
                  {route.name}
                </Text>
              ) : (
                renderLinkWithAnchor('/routes', route.name, 'fa-route')
              )}
              {index < displayedRoutes.length - 1 && <Text mr={1}>,</Text>}
            </React.Fragment>
          ))}
          {hasMore && !isExpanded && (
            <ActionButton
              onClick={() => toggleExpandRoutes(itemName)}
              size="xs"
              ml={1}
            >
              ...
            </ActionButton>
          )}
        </Flex>
      </Box>
    );
  };

  return (
    <Box mt={2}>
      {renderRouteList(activeRoutes, true)}
      {renderRouteList(disabledRoutes, false)}
      {isExpanded && (
        <ActionButton
          onClick={() => toggleExpandRoutes(itemName)}
          size="xs"
          mt={1}
        >
          Show less
        </ActionButton>
      )}
    </Box>
  );
};

export default ItemRoutes;

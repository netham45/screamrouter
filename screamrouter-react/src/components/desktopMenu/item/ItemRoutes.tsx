/**
 * Compact routes display component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React, { useState } from 'react';
import { Box, Text, Flex, useColorModeValue } from '@chakra-ui/react';
import { Route } from '../../../api/api';
import ActionButton from '../controls/ActionButton';

interface ItemRoutesProps {
  /**
   * Routes associated with the item
   */
  routes: Route[];
  
  /**
   * Function to call when a route is clicked
   */
  onNavigate: (routeName: string) => void;
}

/**
 * A compact routes display component optimized for the DesktopMenu interface.
 */
const ItemRoutes: React.FC<ItemRoutesProps> = ({
  routes,
  onNavigate
}) => {
  const [isExpanded, setIsExpanded] = useState(false);
  
  // Color values for light/dark mode
  const linkColor = useColorModeValue('blue.600', 'blue.300');
  
  // If there are no routes, don't render anything
  if (!routes || routes.length === 0) {
    return null;
  }
  
  // Determine which routes to display based on expansion state
  const displayedRoutes = isExpanded ? routes : routes.slice(0, 2);
  const hasMore = routes.length > 2;
  
  return (
    <Box mt={1} fontSize="sm">
      <Flex wrap="wrap" alignItems="center">
        {displayedRoutes.map((route, index) => (
          <React.Fragment key={route.name}>
            <Text
              onClick={() => onNavigate(route.name)}
              cursor="pointer"
              textDecoration="underline"
              color={linkColor}
              opacity={route.enabled ? 1 : 0.6}
              mr={index < displayedRoutes.length - 1 ? 1 : 0}
            >
              {route.name}
            </Text>
            {index < displayedRoutes.length - 1 && <Text mr={1}>,</Text>}
          </React.Fragment>
        ))}
        {hasMore && !isExpanded && (
          <ActionButton
            icon="chevron-down"
            onClick={() => setIsExpanded(true)}
            size="xs"
            ml={1}
          />
        )}
        {isExpanded && (
          <ActionButton
            icon="chevron-up"
            onClick={() => setIsExpanded(false)}
            size="xs"
            ml={1}
          />
        )}
      </Flex>
    </Box>
  );
};

export default ItemRoutes;
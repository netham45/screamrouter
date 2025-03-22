/**
 * Compact route item component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { Tr, Td, Text, Flex, useColorModeValue, Box } from '@chakra-ui/react';
import { Route } from '../../../api/api';
import { DesktopMenuActions } from '../types';
import ActionButton from '../controls/ActionButton';
import VolumeControl from '../controls/VolumeControl';

interface RouteItemProps {
  /**
   * Route object
   */
  route: Route;
  
  /**
   * Whether the route is starred
   */
  isStarred: boolean;
  
  /**
   * Actions for the DesktopMenu
   */
  actions: DesktopMenuActions;
  
  /**
   * Whether the item is selected
   */
  isSelected?: boolean;
}

/**
 * A compact route item component optimized for the DesktopMenu interface.
 */
const RouteItem: React.FC<RouteItemProps> = ({
  route,
  isStarred,
  actions,
  isSelected = false
}) => {
  // Color values for light/dark mode
  const selectedBg = useColorModeValue('blue.50', 'blue.900');
  
  return (
    <Tr 
      bg={isSelected ? selectedBg : undefined}
      id={`routes-${encodeURIComponent(route.name)}`}
    >
      {/* Name */}
      <Td>
        <Flex alignItems="center">
          <ActionButton
            icon="star"
            isActive={isStarred}
            onClick={() => actions.toggleStar('routes', route.name)}
            mr={2}
          />
          <Text fontWeight="medium">{route.name}</Text>
        </Flex>
        <Text fontSize="sm">
          <Text as="span" fontWeight="medium">From:</Text> {route.source}
        </Text>
        <Text fontSize="sm">
          <Text as="span" fontWeight="medium">To:</Text> {route.sink}
        </Text>
      </Td>
      
      {/* Status */}
      <Td>
        <ActionButton
          icon={route.enabled ? 'check' : 'x'}
          isActive={route.enabled}
          onClick={() => actions.toggleEnabled('routes', route.name, !route.enabled)}
        />
      </Td>
      
      {/* Volume Control */}
      <Td>
        <Box position="relative" minW="250px">
          <VolumeControl
            maxWidth="250px"
            value={route.volume}
            onChange={(value) => actions.updateVolume('routes', route.name, value)}
          />
        </Box>
      </Td>
    </Tr>
  );
};

export default RouteItem;
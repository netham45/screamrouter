/**
 * Compact route list component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { Box, Table, Thead, Tbody, Tr, Th, useColorModeValue } from '@chakra-ui/react';
import { Route } from '../../../api/api';
import RouteItem from '../item/RouteItem';
import { DesktopMenuActions } from '../types';

interface RouteListProps {
  /**
   * Array of routes to display
   */
  routes: Route[];
  
  /**
   * Array of starred route names
   */
  starredRoutes: string[];
  
  /**
   * Actions for the DesktopMenu
   */
  actions: DesktopMenuActions;
  
  /**
   * Name of the selected route
   */
  selectedItem?: string | null;
}

/**
 * A compact route list component optimized for the DesktopMenu interface.
 */
const RouteList: React.FC<RouteListProps> = ({
  routes,
  starredRoutes,
  actions,
  selectedItem
}) => {
  // Color values for light/dark mode
  const tableHeaderBg = useColorModeValue('gray.50', 'gray.700');
  
  return (
    <Box overflowX="auto" width="100%">
      <Table variant="simple" size="sm" width="100%">
        <Thead>
          <Tr>
            <Th bg={tableHeaderBg}>Name</Th>
            <Th bg={tableHeaderBg} width="40px">Status</Th>
            <Th bg={tableHeaderBg} width="100px">Volume</Th>
          </Tr>
        </Thead>
        <Tbody>
          {routes.map(route => (
            <RouteItem
              key={route.name}
              route={route}
              isStarred={starredRoutes.includes(route.name)}
              actions={actions}
              isSelected={selectedItem === route.name}
            />
          ))}
        </Tbody>
      </Table>
    </Box>
  );
};

export default RouteList;
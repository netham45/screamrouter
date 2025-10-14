import React from 'react';
import { Box, SimpleGrid, VStack, useColorModeValue } from '@chakra-ui/react';
import { ContentProps } from '../types';
import ResourceCard from '../cards/ResourceCard';
import ResourceListItem from '../cards/ResourceListItem';
import { sortRoutes } from '../utils';

/**
 * RoutesContent component for the FullMenu.
 * This component displays a list of all routes in either grid or list view.
 */
const RoutesContent: React.FC<ContentProps> = ({
  routes,
  starredRoutes,
  viewMode,
  sortConfig,
  handleStar,
  handleToggleRoute,
  handleOpenRouteEqualizer,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  actions
}) => {
  // Sort routes based on the current sort configuration
  const sortedRoutes = sortRoutes(routes, sortConfig, starredRoutes);
  
  // Define colors based on color mode
  const bgColor = useColorModeValue('white', 'gray.800');

  return (
    <Box>
      {viewMode === 'grid' ? (
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {sortedRoutes.map(route => (
            <ResourceCard
              key={`route-${route.name}`}
              item={route}
              type="routes"
              isStarred={starredRoutes.includes(route.name)}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onEqualizer={() => handleOpenRouteEqualizer(route.name)}
              onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
              onEdit={() => actions.editItem('routes', route)}
              onChannelMapping={() => actions.openChannelMapping('routes', route)}
              onDelete={() => actions.deleteItem('routes', route.name)}
              routes={routes}
              navigate={actions.navigate}
            />
          ))}
        </SimpleGrid>
      ) : (
        <VStack spacing={2} align="stretch" bg={bgColor} borderRadius="md">
          {sortedRoutes.map(route => (
            <ResourceListItem
              key={`route-${route.name}`}
              item={route}
              type="routes"
              isStarred={starredRoutes.includes(route.name)}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onEqualizer={() => handleOpenRouteEqualizer(route.name)}
              onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
              onEdit={() => actions.editItem('routes', route)}
              routes={routes}
              navigate={actions.navigate}
            />
          ))}
        </VStack>
      )}
    </Box>
  );
};

export default RoutesContent;
import React from 'react';
import {
  Box,
  Heading,
  Flex,
  Icon,
  SimpleGrid,
  VStack,
  useColorModeValue
} from '@chakra-ui/react';
import { FaMicrophone, FaVolumeUp, FaRoute } from 'react-icons/fa';
import { ContentProps } from '../types';
import ResourceCard from '../cards/ResourceCard';
import ResourceListItem from '../cards/ResourceListItem';
import EmptyState from './EmptyState';

/**
 * FavoritesContent component for the FullMenu.
 * This component displays all starred items grouped by type.
 */
const FavoritesContent: React.FC<ContentProps> = ({
  sources,
  sinks,
  routes,
  starredSources,
  starredSinks,
  starredRoutes,
  contextActiveSource,
  listeningToSink,
  viewMode,
  handleStar,
  handleToggleSource,
  handleToggleSink,
  handleToggleRoute,
  handleOpenSourceEqualizer,
  handleOpenSinkEqualizer,
  handleOpenRouteEqualizer,
  handleOpenVnc,
  handleUpdateSourceVolume,
  handleUpdateSinkVolume,
  handleUpdateSourceTimeshift,
  handleUpdateSinkTimeshift,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  handleControlSource,
  // Required by ContentProps but not used in this component
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  setCurrentCategory,
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  sortConfig,
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  actions,
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  handleOpenVisualizer
}) => {
  const starredSourceItems = sources.filter(source => starredSources.includes(source.name));
  const starredSinkItems = sinks.filter(sink => starredSinks.includes(sink.name));
  const starredRouteItems = routes.filter(route => starredRoutes.includes(route.name));
  
  if (starredSourceItems.length === 0 && starredSinkItems.length === 0 && starredRouteItems.length === 0) {
    return (
      <EmptyState
        icon="star"
        title="No Favorites"
        message="Star items to add them to your favorites"
      />
    );
  }
  
  // Define colors based on color mode
  const sectionBg = useColorModeValue('white', 'gray.800');
  const sectionBorderColor = useColorModeValue('gray.200', 'gray.700');
  const headingColor = useColorModeValue('gray.700', 'white');
  const bgColor = useColorModeValue('white', 'gray.800');

  return (
    <Box>
      {starredSourceItems.length > 0 && (
        <Box
          mb={6}
          p={4}
          borderWidth="1px"
          borderRadius="lg"
          borderColor={sectionBorderColor}
          bg={sectionBg}
        >
          <Flex align="center" mb={4}>
            <Icon as={FaMicrophone} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Favorite Sources
            </Heading>
          </Flex>
          
          <Box>
            {viewMode === 'grid' ? (
              <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
                {starredSourceItems.map(source => (
                  <ResourceCard
                    key={`favorite-source-${source.name}`}
                    item={source}
                    type="sources"
                    isStarred={true}
                    isActive={contextActiveSource === source.name}
                    onStar={() => handleStar('sources', source.name)}
                    onActivate={() => handleToggleSource(source.name)}
                    onEqualizer={() => handleOpenSourceEqualizer(source.name)}
                    onVnc={source.vnc_ip ? () => handleOpenVnc(source.name) : undefined}
                    onUpdateVolume={(volume) => handleUpdateSourceVolume(source.name, volume)}
                    onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(source.name, timeshift)}
                    onControlSource={source.vnc_ip ? (action) => handleControlSource ? handleControlSource(source.name, action) : {} : undefined}
                    onToggleActiveSource={() => handleToggleSource(source.name)}
                    routes={routes}
                    allSources={sources}
                    allSinks={sinks}
                    navigateToDetails={() => {
                      localStorage.setItem('currentSourceName', source.name);
                      window.location.hash = '#/source';
                    }}
                    navigate={(type, name) => {
                      // Navigate to the appropriate detailed view
                      if (type === 'routes') {
                        localStorage.setItem('currentRouteName', name);
                        window.location.hash = '#/route';
                      } else if (type === 'sources' || type === 'group-source') {
                        localStorage.setItem('currentSourceName', name);
                        window.location.hash = '#/source';
                      } else if (type === 'sinks' || type === 'group-sink') {
                        localStorage.setItem('currentSinkName', name);
                        window.location.hash = '#/sink';
                      }
                    }}
                  />
                ))}
              </SimpleGrid>
            ) : (
              <VStack spacing={2} align="stretch" bg={bgColor} borderRadius="md">
                {starredSourceItems.map(source => (
                  <ResourceListItem
                    key={`favorite-source-${source.name}`}
                    item={source}
                    type="sources"
                    isStarred={true}
                    isActive={contextActiveSource === source.name}
                    onStar={() => handleStar('sources', source.name)}
                    onActivate={() => handleToggleSource(source.name)}
                    onEqualizer={() => handleOpenSourceEqualizer(source.name)}
                    onVnc={source.vnc_ip ? () => handleOpenVnc(source.name) : undefined}
                    onUpdateVolume={(volume) => handleUpdateSourceVolume(source.name, volume)}
                    onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(source.name, timeshift)}
                    onControlSource={source.vnc_ip ? (action) => handleControlSource ? handleControlSource(source.name, action) : {} : undefined}
                    onToggleActiveSource={() => handleToggleSource(source.name)}
                    routes={routes}
                    allSources={sources}
                    allSinks={sinks}
                    navigateToDetails={() => {
                      localStorage.setItem('currentSourceName', source.name);
                      window.location.hash = '#/source';
                    }}
                    navigate={(type, name) => {
                      // Navigate to the appropriate detailed view
                      if (type === 'routes') {
                        localStorage.setItem('currentRouteName', name);
                        window.location.hash = '#/route';
                      } else if (type === 'sources' || type === 'group-source') {
                        localStorage.setItem('currentSourceName', name);
                        window.location.hash = '#/source';
                      } else if (type === 'sinks' || type === 'group-sink') {
                        localStorage.setItem('currentSinkName', name);
                        window.location.hash = '#/sink';
                      }
                    }}
                  />
                ))}
              </VStack>
            )}
          </Box>
        </Box>
      )}
      
      {starredSinkItems.length > 0 && (
        <Box
          mb={6}
          p={4}
          borderWidth="1px"
          borderRadius="lg"
          borderColor={sectionBorderColor}
          bg={sectionBg}
        >
          <Flex align="center" mb={4}>
            <Icon as={FaVolumeUp} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Favorite Sinks
            </Heading>
          </Flex>
          
          <Box>
            {viewMode === 'grid' ? (
              <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
                {starredSinkItems.map(sink => (
                  <ResourceCard
                    key={`favorite-sink-${sink.name}`}
                    item={sink}
                    type="sinks"
                    isStarred={true}
                    isActive={listeningToSink?.name === sink.name}
                    onStar={() => handleStar('sinks', sink.name)}
                    onActivate={() => handleToggleSink(sink.name)}
                    onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
                    onListen={() => handleToggleSink(sink.name)}
                    onVisualize={() => handleOpenSinkEqualizer(sink.name)}
                    onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
                    onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
                    routes={routes}
                    allSources={sources}
                    allSinks={sinks}
                    navigateToDetails={() => {
                      localStorage.setItem('currentSinkName', sink.name);
                      window.location.hash = '#/sink';
                    }}
                    navigate={(type, name) => {
                      // Navigate to the appropriate detailed view
                      if (type === 'routes') {
                        localStorage.setItem('currentRouteName', name);
                        window.location.hash = '#/route';
                      } else if (type === 'sources' || type === 'group-source') {
                        localStorage.setItem('currentSourceName', name);
                        window.location.hash = '#/source';
                      } else if (type === 'sinks' || type === 'group-sink') {
                        localStorage.setItem('currentSinkName', name);
                        window.location.hash = '#/sink';
                      }
                    }}
                  />
                ))}
              </SimpleGrid>
            ) : (
              <VStack spacing={2} align="stretch" bg={bgColor} borderRadius="md">
                {starredSinkItems.map(sink => (
                  <ResourceListItem
                    key={`favorite-sink-${sink.name}`}
                    item={sink}
                    type="sinks"
                    isStarred={true}
                    isActive={listeningToSink?.name === sink.name}
                    onStar={() => handleStar('sinks', sink.name)}
                    onActivate={() => handleToggleSink(sink.name)}
                    onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
                    onListen={() => handleToggleSink(sink.name)}
                    onVisualize={() => handleOpenSinkEqualizer(sink.name)}
                    onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
                    onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
                    routes={routes}
                    allSources={sources}
                    allSinks={sinks}
                    navigateToDetails={() => {
                      localStorage.setItem('currentSinkName', sink.name);
                      window.location.hash = '#/sink';
                    }}
                    navigate={(type, name) => {
                      // Navigate to the appropriate detailed view
                      if (type === 'routes') {
                        localStorage.setItem('currentRouteName', name);
                        window.location.hash = '#/route';
                      } else if (type === 'sources' || type === 'group-source') {
                        localStorage.setItem('currentSourceName', name);
                        window.location.hash = '#/source';
                      } else if (type === 'sinks' || type === 'group-sink') {
                        localStorage.setItem('currentSinkName', name);
                        window.location.hash = '#/sink';
                      }
                    }}
                  />
                ))}
              </VStack>
            )}
          </Box>
        </Box>
      )}
      
      {starredRouteItems.length > 0 && (
        <Box
          mb={6}
          p={4}
          borderWidth="1px"
          borderRadius="lg"
          borderColor={sectionBorderColor}
          bg={sectionBg}
        >
          <Flex align="center" mb={4}>
            <Icon as={FaRoute} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Favorite Routes
            </Heading>
          </Flex>
          
          <Box>
            {viewMode === 'grid' ? (
              <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
                {starredRouteItems.map(route => (
                  <ResourceCard
                    key={`favorite-route-${route.name}`}
                    item={route}
                    type="routes"
                    isStarred={true}
                    isActive={route.enabled}
                    onStar={() => handleStar('routes', route.name)}
                    onActivate={() => handleToggleRoute(route.name)}
                    onEqualizer={() => handleOpenRouteEqualizer(route.name)}
                    onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
                    onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
                    routes={routes}
                    allSources={sources}
                    allSinks={sinks}
                    navigateToDetails={() => {
                      localStorage.setItem('currentRouteName', route.name);
                      window.location.hash = '#/route';
                    }}
                    navigate={(type, name) => {
                      // Navigate to the appropriate detailed view
                      if (type === 'routes') {
                        localStorage.setItem('currentRouteName', name);
                        window.location.hash = '#/route';
                      } else if (type === 'sources' || type === 'group-source') {
                        localStorage.setItem('currentSourceName', name);
                        window.location.hash = '#/source';
                      } else if (type === 'sinks' || type === 'group-sink') {
                        localStorage.setItem('currentSinkName', name);
                        window.location.hash = '#/sink';
                      }
                    }}
                  />
                ))}
              </SimpleGrid>
            ) : (
              <VStack spacing={2} align="stretch" bg={bgColor} borderRadius="md">
                {starredRouteItems.map(route => (
                  <ResourceListItem
                    key={`favorite-route-${route.name}`}
                    item={route}
                    type="routes"
                    isStarred={true}
                    isActive={route.enabled}
                    onStar={() => handleStar('routes', route.name)}
                    onActivate={() => handleToggleRoute(route.name)}
                    onEqualizer={() => handleOpenRouteEqualizer(route.name)}
                    onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
                    onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
                    routes={routes}
                    allSources={sources}
                    allSinks={sinks}
                    navigateToDetails={() => {
                      localStorage.setItem('currentRouteName', route.name);
                      window.location.hash = '#/route';
                    }}
                    navigate={(type, name) => {
                      // Navigate to the appropriate detailed view
                      if (type === 'routes') {
                        localStorage.setItem('currentRouteName', name);
                        window.location.hash = '#/route';
                      } else if (type === 'sources' || type === 'group-source') {
                        localStorage.setItem('currentSourceName', name);
                        window.location.hash = '#/source';
                      } else if (type === 'sinks' || type === 'group-sink') {
                        localStorage.setItem('currentSinkName', name);
                        window.location.hash = '#/sink';
                      }
                    }}
                  />
                ))}
              </VStack>
            )}
          </Box>
        </Box>
      )}
    </Box>
  );
};

export default FavoritesContent;
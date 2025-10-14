import React, { useEffect, useState } from 'react';
import {
  Box,
  Heading,
  Flex,
  SimpleGrid,
  Icon,
  useColorModeValue
} from '@chakra-ui/react';
import { FaMicrophone, FaVolumeUp } from 'react-icons/fa';
import { ContentProps } from '../types';
import EmptyState from './EmptyState';
import ResourceCard from '../cards/ResourceCard';

/**
 * RouteContent component for the FullMenu.
 * This component displays detailed information about a specific route.
 */
const RouteContent: React.FC<ContentProps> = ({
  sources,
  sinks,
  routes,
  starredRoutes,
  listeningToSink,
  setCurrentCategory,
  handleStar,
  handleToggleRoute,
  handleToggleSource,
  handleToggleSink,
  handleOpenRouteEqualizer,
  handleOpenSourceEqualizer,
  handleOpenSinkEqualizer,
  handleOpenVnc,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  handleUpdateSourceVolume,
  handleUpdateSourceTimeshift,
  handleUpdateSinkVolume,
  handleUpdateSinkTimeshift,
  handleControlSource,
  actions
}) => {
  // State to store the route name
  const [routeName, setRouteName] = useState<string | null>(null);
  
  // Effect to load the route name from localStorage
  useEffect(() => {
    const storedRouteName = localStorage.getItem('currentRouteName');
    if (storedRouteName) {
      setRouteName(storedRouteName);
    }
  }, []);
  
  // Find the route by name
  const route = routes.find(r => r.name === routeName);
  
  // Define colors based on color mode
  const headingColor = useColorModeValue('gray.700', 'white');
  
  if (!route) {
    return (
      <EmptyState
        icon="route"
        title="Route Not Found"
        message="The requested route could not be found"
        actionText="View All Routes"
        onAction={() => setCurrentCategory('routes')}
      />
    );
  }
  
  // Find the source and sink for this route
  const source = sources.find(s => s.name === route.source);
  const sink = sinks.find(s => s.name === route.sink);
  
  return (
    <Box>
      <Box mb={6}>
        <Heading as="h2" size="lg" color={headingColor} mb={4}>
          Route: {route.name}
        </Heading>
        
        <ResourceCard
          key={`route-${route.name}`}
          item={route}
          type="routes"
          isStarred={starredRoutes?.includes(route.name) || false}
          isActive={route.enabled}
          onStar={() => handleStar('routes', route.name)}
          onActivate={() => handleToggleRoute(route.name)}
          onEqualizer={() => handleOpenRouteEqualizer(route.name)}
          onChannelMapping={() => actions.openChannelMapping('routes', route)}
          onDelete={() => actions.deleteItem('routes', route.name)}
          onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
          onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
          routes={routes}
          allSources={sources}
          allSinks={sinks}
          navigateToDetails={undefined}
          navigate={(type, name) => {
            // Navigate to the appropriate detailed view
            if (type === 'routes') {
              localStorage.setItem('currentRouteName', name);
              setCurrentCategory('route');
              console.log(`Navigating to route detail: ${name}`);
            } else if (type === 'sources' || type === 'group-source') {
              localStorage.setItem('currentSourceName', name);
              setCurrentCategory('source');
              console.log(`Navigating to source detail: ${name}`);
            } else if (type === 'sinks' || type === 'group-sink') {
              localStorage.setItem('currentSinkName', name);
              setCurrentCategory('sink');
              console.log(`Navigating to sink detail: ${name}`);
            }
          }}
        />
      </Box>
      
      {source && (
        <Box mb={6}>
          <Flex align="center" mb={4}>
            <Icon as={FaMicrophone} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Source
            </Heading>
          </Flex>
          
          <SimpleGrid columns={{ base: 1, md: 1, lg: 1 }} spacing={4}>
            <ResourceCard
              key={`route-source-${source.name}`}
              item={source}
              type="sources"
              isStarred={starredRoutes?.includes(source.name) || false}
              isActive={source.enabled}
              onStar={() => handleStar('sources', source.name)}
              onActivate={() => handleToggleSource(source.name)}
              onEqualizer={() => handleOpenSourceEqualizer(source.name)}
              onVnc={source.vnc_ip && handleOpenVnc ? () => handleOpenVnc(source.name) : undefined}
              onChannelMapping={() => actions.openChannelMapping('sources', source)}
              onDelete={() => actions.deleteItem('sources', source.name)}
              onUpdateVolume={(volume) => handleUpdateSourceVolume(source.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(source.name, timeshift)}
              onControlSource={source.vnc_ip && handleControlSource ? (action) => handleControlSource(source.name, action) : undefined}
              onToggleActiveSource={() => handleToggleSource(source.name)}
              routes={routes}
              allSources={sources}
              allSinks={sinks}
              navigateToDetails={() => {
                // Store the source name in localStorage
                localStorage.setItem('currentSourceName', source.name);
                // Navigate to the source details page
                setCurrentCategory('source');
              }}
              navigate={(type, name) => {
                // Navigate to the appropriate detailed view
                if (type === 'routes') {
                  localStorage.setItem('currentRouteName', name);
                  setCurrentCategory('route');
                  console.log(`Navigating to route detail: ${name}`);
                } else if (type === 'sources' || type === 'group-source') {
                  localStorage.setItem('currentSourceName', name);
                  setCurrentCategory('source');
                  console.log(`Navigating to source detail: ${name}`);
                } else if (type === 'sinks' || type === 'group-sink') {
                  localStorage.setItem('currentSinkName', name);
                  setCurrentCategory('sink');
                  console.log(`Navigating to sink detail: ${name}`);
                }
              }}
            />
          </SimpleGrid>
        </Box>
      )}
      
      {sink && (
        <Box mb={6}>
          <Flex align="center" mb={4}>
            <Icon as={FaVolumeUp} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Sink
            </Heading>
          </Flex>
          
          <SimpleGrid columns={{ base: 1, md: 1, lg: 1 }} spacing={4}>
            <ResourceCard
              key={`route-sink-${sink.name}`}
              item={sink}
              type="sinks"
              isStarred={starredRoutes?.includes(sink.name) || false}
              isActive={listeningToSink?.name === sink.name}
              onStar={() => handleStar('sinks', sink.name)}
              onActivate={() => handleToggleSink(sink.name)}
              onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
              onChannelMapping={() => actions.openChannelMapping('sinks', sink)}
              onDelete={() => actions.deleteItem('sinks', sink.name)}
              onListen={() => {
                // Open the listen page for this sink
                window.open(`/site/listen/${encodeURIComponent(sink.name)}`, '_blank');
              }}
              onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
              routes={routes}
              allSources={sources}
              allSinks={sinks}
              navigateToDetails={() => {
                // Store the sink name in localStorage
                localStorage.setItem('currentSinkName', sink.name);
                // Navigate to the sink details page
                setCurrentCategory('sink');
              }}
              navigate={(type, name) => {
                // Navigate to the appropriate detailed view
                if (type === 'routes') {
                  localStorage.setItem('currentRouteName', name);
                  setCurrentCategory('route');
                  console.log(`Navigating to route detail: ${name}`);
                } else if (type === 'sources' || type === 'group-source') {
                  localStorage.setItem('currentSourceName', name);
                  setCurrentCategory('source');
                  console.log(`Navigating to source detail: ${name}`);
                } else if (type === 'sinks' || type === 'group-sink') {
                  localStorage.setItem('currentSinkName', name);
                  setCurrentCategory('sink');
                  console.log(`Navigating to sink detail: ${name}`);
                }
              }}
            />
          </SimpleGrid>
        </Box>
      )}
    </Box>
  );
};

export default RouteContent;
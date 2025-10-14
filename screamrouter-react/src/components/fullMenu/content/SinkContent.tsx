import React, { useEffect, useState } from 'react';
import {
  Box,
  Heading,
  Flex,
  SimpleGrid,
  Icon,
  Button,
  useColorModeValue
} from '@chakra-ui/react';
import { FaRoute, FaPlus } from 'react-icons/fa';
import { ContentProps } from '../types';
import EmptyState from './EmptyState';
import ResourceCard from '../cards/ResourceCard';
import { openAddRouteWithPreselection } from '../utils';

/**
 * SinkContent component for the FullMenu.
 * This component displays detailed information about a specific sink.
 */
const SinkContent: React.FC<ContentProps> = ({
  sources,
  sinks,
  routes,
  starredSinks,
  listeningToSink,
  setCurrentCategory,
  handleStar,
  handleToggleSink,
  handleToggleRoute,
  handleOpenSinkEqualizer,
  handleUpdateSinkVolume,
  handleUpdateSinkTimeshift,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  actions
}) => {
  // State to store the sink name
  const [sinkName, setSinkName] = useState<string | null>(null);
  
  // Effect to load the sink name from localStorage
  useEffect(() => {
    const storedSinkName = localStorage.getItem('currentSinkName');
    if (storedSinkName) {
      setSinkName(storedSinkName);
    }
  }, []);
  
  // Find the sink by name
  const sink = sinks.find(s => s.name === sinkName);
  
  // Define colors based on color mode
  const headingColor = useColorModeValue('gray.700', 'white');
  
  if (!sink) {
    return (
      <EmptyState
        icon="volume-up"
        title="Sink Not Found"
        message="The requested sink could not be found"
        actionText="View All Sinks"
        onAction={() => setCurrentCategory('sinks')}
      />
    );
  }
  
  return (
    <Box>
      <Box mb={6}>
        <Heading as="h2" size="lg" color={headingColor} mb={4}>
          Sink: {sink.name}
        </Heading>
        
        <ResourceCard
          key={`sink-${sink.name}`}
          item={sink}
          type="sinks"
          isStarred={starredSinks?.includes(sink.name) || false}
          isActive={listeningToSink?.name === sink.name}
          onStar={() => handleStar('sinks', sink.name)}
          onActivate={() => handleToggleSink(sink.name)}
          onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
          onChannelMapping={() => actions.openChannelMapping('sinks', sink)}
          onDelete={() => actions.deleteItem('sinks', sink.name)}
          onListen={() => {
            // If this sink is already being listened to, pass null to stop listening
            if (listeningToSink?.name === sink.name) {
              actions.listenToSink(null);
            } else {
              // Otherwise, start listening to this sink
              actions.listenToSink(sink);
            }
          }}
          onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
          onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
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
      
      <Box mb={6}>
        <Flex align="center" justify="space-between" mb={4}>
          <Flex align="center">
            <Icon as={FaRoute} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Connected Routes
            </Heading>
          </Flex>
          <Button
            leftIcon={<Icon as={FaPlus} />}
            colorScheme="blue"
            size="sm"
            onClick={() => openAddRouteWithPreselection('sink', sink.name)}
          >
            Add Route
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {routes.filter(route => route.sink === sink.name).map(route => (
            <ResourceCard
              key={`sink-route-${route.name}`}
              item={route}
              type="routes"
              isStarred={starredSinks?.includes(route.name) || false}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onEqualizer={() => handleOpenSinkEqualizer(route.name)}
              onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
              routes={routes}
              allSources={sources}
              allSinks={sinks}
              navigateToDetails={() => {
                // Store the route name in localStorage
                localStorage.setItem('currentRouteName', route.name);
                // Navigate to the route details page
                setCurrentCategory('route');
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
          ))}
        </SimpleGrid>
      </Box>
    </Box>
  );
};

export default SinkContent;
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
 * SourceContent component for the FullMenu.
 * This component displays detailed information about a specific source.
 */
const SourceContent: React.FC<ContentProps> = ({
  sources,
  sinks,
  routes,
  starredSources,
  contextActiveSource,
  setCurrentCategory,
  handleStar,
  handleToggleSource,
  handleToggleRoute,
  handleOpenSourceEqualizer,
  handleOpenVnc,
  handleUpdateSourceVolume,
  handleUpdateSourceTimeshift,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  handleControlSource
}) => {
  // State to store the source name
  const [sourceName, setSourceName] = useState<string | null>(null);
  
  // Effect to load the source name from localStorage
  useEffect(() => {
    const storedSourceName = localStorage.getItem('currentSourceName');
    if (storedSourceName) {
      setSourceName(storedSourceName);
    }
  }, []);
  
  // Find the source by name
  const source = sources.find(s => s.name === sourceName);
  
  // Define colors based on color mode
  const headingColor = useColorModeValue('gray.700', 'white');
  
  if (!source) {
    return (
      <EmptyState
        icon="microphone"
        title="Source Not Found"
        message="The requested source could not be found"
        actionText="View All Sources"
        onAction={() => setCurrentCategory('sources')}
      />
    );
  }
  
  return (
    <Box>
      <Box mb={6}>
        <Heading as="h2" size="lg" color={headingColor} mb={4}>
          Source: {source.name}
        </Heading>
        
        <ResourceCard
          key={`source-${source.name}`}
          item={source}
          type="sources"
          isStarred={starredSources.includes(source.name)}
          isActive={contextActiveSource === source.name}
          onStar={() => handleStar('sources', source.name)}
          onActivate={() => handleToggleSource(source.name)}
          onEqualizer={() => handleOpenSourceEqualizer(source.name)}
          onVnc={source.vnc_ip ? () => handleOpenVnc(source.name) : undefined}
          onChannelMapping={() => actions.openChannelMapping('sources', source)}
          onDelete={() => actions.deleteItem('sources', source.name)}
          onUpdateVolume={(volume) => handleUpdateSourceVolume(source.name, volume)}
          onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(source.name, timeshift)}
          onControlSource={source.vnc_ip && handleControlSource ? (action) => handleControlSource(source.name, action) : undefined}
          onToggleActiveSource={() => handleToggleSource(source.name)}
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
            onClick={() => openAddRouteWithPreselection('source', source.name)}
          >
            Add Route
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {routes.filter(route => route.source === source.name).map(route => (
            <ResourceCard
              key={`source-route-${route.name}`}
              item={route}
              type="routes"
              isStarred={starredSources.includes(route.name)}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onEqualizer={() => actions.showEqualizer(true, 'routes', route)}
              onChannelMapping={() => actions.openChannelMapping('routes', route)}
              onDelete={() => actions.deleteItem('routes', route.name)}
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

export default SourceContent;
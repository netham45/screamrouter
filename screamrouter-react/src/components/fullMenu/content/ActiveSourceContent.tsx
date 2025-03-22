import React from 'react';
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
 * ActiveSourceContent component for the FullMenu.
 * This component displays detailed information about the Primary Source.
 */
const ActiveSourceContent: React.FC<ContentProps> = ({
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
  const primarySource = sources.find(source => source.name === contextActiveSource);
  
  // Define colors based on color mode
  const headingColor = useColorModeValue('gray.700', 'white');
  
  if (!primarySource) {
    return (
      <EmptyState
        icon="broadcast-tower"
        title="No Primary Source"
        message="Select a source to make it active"
        actionText="View Sources"
        onAction={() => setCurrentCategory('sources')}
      />
    );
  }
  
  return (
    <Box>
      <Box mb={6}>
        <Heading as="h2" size="lg" color={headingColor} mb={4}>
          Primary Source
        </Heading>
        
        <ResourceCard
          key={`primary-source-${primarySource.name}`}
          item={primarySource}
          type="sources"
          isStarred={starredSources.includes(primarySource.name)}
          isActive={true}
          onStar={() => handleStar('sources', primarySource.name)}
          onActivate={() => handleToggleSource(primarySource.name)}
          onEqualizer={() => handleOpenSourceEqualizer(primarySource.name)}
          onVnc={primarySource.vnc_ip ? () => handleOpenVnc(primarySource.name) : undefined}
          onUpdateVolume={(volume) => handleUpdateSourceVolume(primarySource.name, volume)}
          onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(primarySource.name, timeshift)}
          onControlSource={primarySource.vnc_ip && handleControlSource ? (action) => handleControlSource(primarySource.name, action) : undefined}
          onToggleActiveSource={() => handleToggleSource(primarySource.name)}
          routes={routes}
          allSources={sources}
          allSinks={sinks}
          navigateToDetails={undefined}
          navigate={(type, name) => {
            // Navigate to the appropriate category and log the item name
            if (type === 'routes') {
              setCurrentCategory('routes');
              console.log(`Navigating to route: ${name}`);
            } else if (type === 'sources' || type === 'group-source') {
              setCurrentCategory('sources');
              console.log(`Navigating to source: ${name}`);
            } else if (type === 'sinks' || type === 'group-sink') {
              setCurrentCategory('sinks');
              console.log(`Navigating to sink: ${name}`);
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
            onClick={() => openAddRouteWithPreselection('source', primarySource.name)}
          >
            Add Route
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {routes.filter(route => route.source === primarySource.name).map(route => (
            <ResourceCard
              key={`source-route-${route.name}`}
              item={route}
              type="routes"
              isStarred={starredSources.includes(route.name)}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onEqualizer={() => handleOpenSourceEqualizer(route.name)}
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
                // Navigate to the appropriate category and log the item name
                if (type === 'routes') {
                  setCurrentCategory('routes');
                  console.log(`Navigating to route: ${name}`);
                } else if (type === 'sources' || type === 'group-source') {
                  setCurrentCategory('sources');
                  console.log(`Navigating to source: ${name}`);
                } else if (type === 'sinks' || type === 'group-sink') {
                  setCurrentCategory('sinks');
                  console.log(`Navigating to sink: ${name}`);
                }
              }}
            />
          ))}
        </SimpleGrid>
      </Box>
    </Box>
  );
};

export default ActiveSourceContent;
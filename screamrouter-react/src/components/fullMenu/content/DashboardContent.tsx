import React from 'react';
import {
  Box,
  Heading,
  Flex,
  Button,
  Text,
  Icon,
  SimpleGrid,
  useColorModeValue
} from '@chakra-ui/react';
import { StarIcon } from '@chakra-ui/icons';
import { ContentProps } from '../types';
import ResourceCard from '../cards/ResourceCard';
import StatusCard from '../cards/StatusCard';

/**
 * DashboardContent component for the FullMenu.
 * This component displays an overview of the system status, favorites, and active items.
 */
const DashboardContent: React.FC<ContentProps> = ({
  sources,
  sinks,
  routes,
  starredSources,
  starredSinks,
  starredRoutes,
  contextActiveSource,
  listeningToSink,
  setCurrentCategory,
  handleStar,
  handleToggleSource,
  handleToggleSink,
  handleToggleRoute,
  handleOpenSourceEqualizer,
  handleOpenSinkEqualizer,
  handleOpenRouteEqualizer,
  handleOpenVnc,
  handleOpenVisualizer,
  handleUpdateSourceVolume,
  handleUpdateSinkVolume,
  handleUpdateSourceTimeshift,
  handleUpdateSinkTimeshift,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  handleControlSource,
  actions
}) => {
  const primarySource = sources.find(source => source.name === contextActiveSource);
  
  // Define colors based on color mode
  const sectionBg = useColorModeValue('white', 'gray.800');
  const sectionBorderColor = useColorModeValue('gray.200', 'gray.700');
  const headingColor = useColorModeValue('gray.700', 'white');
  // Use blue colors for buttons to match the rest of the UI
  const buttonColorScheme = "blue";

  return (
    <Box>
      {/* Status Overview */}
      <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4} mb={6}>
        <StatusCard
          title="Primary Source"
          icon="broadcast-tower"
          status={primarySource ? "Active" : "None"}
          isPositive={!!primarySource}
          onClick={() => setCurrentCategory('active-source')}
        >
          {primarySource && (
            <Box>
              <Text fontWeight="bold" mb={1}>{primarySource.name}</Text>
              <Text>Volume: {primarySource.volume}%</Text>
              {primarySource.timeshift !== undefined && (
                <Text>Timeshift: {primarySource.timeshift}ms</Text>
              )}
            </Box>
          )}
        </StatusCard>
        
        <StatusCard
          title="Now Listening"
          icon="headphones"
          status={listeningToSink ? "Active" : "None"}
          isPositive={!!listeningToSink}
          onClick={() => setCurrentCategory('now-listening')}
        >
          {listeningToSink && (
            <Box>
              <Text fontWeight="bold" mb={1}>{listeningToSink.name}</Text>
              {listeningToSink.volume !== undefined && (
                <Text>Volume: {listeningToSink.volume}%</Text>
              )}
            </Box>
          )}
        </StatusCard>
        
        <StatusCard
          title="System Status"
          icon="server"
          status="Online"
          isPositive={true}
        >
          <Box>
            <Text>Primary Sources: {sources.filter(s => s.enabled).length}</Text>
            <Text>Active Sinks: {sinks.filter(s => s.enabled).length}</Text>
            <Text>Active Routes: {routes.filter(r => r.enabled).length}</Text>
          </Box>
        </StatusCard>
      </SimpleGrid>
      
      {/* Primary Source Section */}
      {primarySource && (
        <Box
          mb={6}
          p={4}
          borderWidth="1px"
          borderRadius="lg"
          borderColor={sectionBorderColor}
          bg={sectionBg}
        >
          <Flex justify="space-between" align="center" mb={4}>
            <Flex align="center">
              <Box as="i" className="fas fa-broadcast-tower" mr={2} />
              <Heading as="h3" size="md" color={headingColor}>
                Primary Source: {primarySource.name}
              </Heading>
            </Flex>
            <Button
              size="sm"
              onClick={() => setCurrentCategory('active-source')}
              colorScheme={buttonColorScheme}
              variant="outline"
            >
              View Details
            </Button>
          </Flex>
          
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
            onControlSource={primarySource.vnc_ip ? (action) => handleControlSource && handleControlSource(primarySource.name, action) : undefined}
            onToggleActiveSource={() => handleToggleSource(primarySource.name)}
            routes={routes}
            allSources={sources}
            allSinks={sinks}
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
      )}
      
      {/* Favorites Section */}
      <Box
        mb={6}
        p={4}
        borderWidth="1px"
        borderRadius="lg"
        borderColor={sectionBorderColor}
        bg={sectionBg}
      >
        <Flex justify="space-between" align="center" mb={4}>
          <Flex align="center">
            <Icon as={StarIcon} mr={2} color="yellow.400" />
            <Heading as="h3" size="md" color={headingColor}>
              Favorites
            </Heading>
          </Flex>
          <Button
            size="sm"
            onClick={() => setCurrentCategory('favorites')}
            colorScheme={buttonColorScheme}
            variant="outline"
          >
            View All
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {starredSources.slice(0, 3).map(sourceName => {
            const source = sources.find(s => s.name === sourceName);
            if (!source) return null;
            
            return (
              <ResourceCard
                key={`source-${source.name}`}
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
                onControlSource={source.vnc_ip ? (action) => handleControlSource && handleControlSource(source.name, action) : undefined}
                onToggleActiveSource={() => handleToggleSource(source.name)}
                routes={routes}
                allSources={sources}
                allSinks={sinks}
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
            );
          })}
          
          {starredSinks.slice(0, 3).map(sinkName => {
            const sink = sinks.find(s => s.name === sinkName);
            if (!sink) return null;
            
            return (
              <ResourceCard
                key={`sink-${sink.name}`}
                item={sink}
                type="sinks"
                isStarred={true}
                isActive={listeningToSink?.name === sink.name}
                onStar={() => handleStar('sinks', sink.name)}
                onActivate={() => handleToggleSink(sink.name)}
                onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
                onListen={() => {
                  // If this sink is already being listened to, pass null to stop listening
                  if (listeningToSink?.name === sink.name) {
                    actions.listenToSink(null);
                  } else {
                    // Otherwise, start listening to this sink
                    actions.listenToSink(sink);
                  }
                }}
                onVisualize={() => handleOpenVisualizer ? handleOpenVisualizer(sink) : {}}
                onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
                onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
                routes={routes}
                allSources={sources}
                allSinks={sinks}
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
            );
          })}
          
          {starredRoutes.slice(0, 3).map(routeName => {
            const route = routes.find(r => r.name === routeName);
            if (!route) return null;
            
            return (
              <ResourceCard
                key={`route-${route.name}`}
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
            );
          })}
        </SimpleGrid>
      </Box>
      
      {/* Active Items Section */}
      <Box
        mb={6}
        p={4}
        borderWidth="1px"
        borderRadius="lg"
        borderColor={sectionBorderColor}
        bg={sectionBg}
      >
        <Flex justify="space-between" align="center" mb={4}>
          <Flex align="center">
            <Box as="i" className="fas fa-broadcast-tower" mr={2} />
            <Heading as="h3" size="md" color={headingColor}>
              Primary Sources
            </Heading>
          </Flex>
          <Button
            size="sm"
            onClick={() => setCurrentCategory('sources')}
            colorScheme={buttonColorScheme}
            variant="outline"
          >
            View All
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {sources.filter(source => source.enabled).slice(0, 10).map(source => (
            <ResourceCard
              key={`active-source-${source.name}`}
              item={source}
              type="sources"
              isStarred={starredSources.includes(source.name)}
              isActive={contextActiveSource === source.name}
              onStar={() => handleStar('sources', source.name)}
              onActivate={() => handleToggleSource(source.name)}
              onEqualizer={() => handleOpenSourceEqualizer(source.name)}
              onVnc={source.vnc_ip ? () => handleOpenVnc(source.name) : undefined}
              onUpdateVolume={(volume) => handleUpdateSourceVolume(source.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(source.name, timeshift)}
              onControlSource={source.vnc_ip ? (action) => handleControlSource && handleControlSource(source.name, action) : undefined}
              onToggleActiveSource={() => handleToggleSource(source.name)}
              routes={routes}
              allSources={sources}
              allSinks={sinks}
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
      
      <Box
        mb={6}
        p={4}
        borderWidth="1px"
        borderRadius="lg"
        borderColor={sectionBorderColor}
        bg={sectionBg}
      >
        <Flex justify="space-between" align="center" mb={4}>
          <Flex align="center">
            <Box as="i" className="fas fa-volume-up" mr={2} />
            <Heading as="h3" size="md" color={headingColor}>
              Active Sinks
            </Heading>
          </Flex>
          <Button
            size="sm"
            onClick={() => setCurrentCategory('sinks')}
            colorScheme={buttonColorScheme}
            variant="outline"
          >
            View All
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {sinks.filter(sink => sink.enabled).slice(0, 10).map(sink => (
            <ResourceCard
              key={`active-sink-${sink.name}`}
              item={sink}
              type="sinks"
              isStarred={starredSinks.includes(sink.name)}
              isActive={listeningToSink?.name === sink.name}
              onStar={() => handleStar('sinks', sink.name)}
              onActivate={() => handleToggleSink(sink.name)}
              onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
              onListen={() => {
                // If this sink is already being listened to, pass null to stop listening
                if (listeningToSink?.name === sink.name) {
                  actions.listenToSink(null);
                } else {
                  // Otherwise, start listening to this sink
                  actions.listenToSink(sink);
                }
              }}
              onVisualize={() => handleOpenVisualizer ? handleOpenVisualizer(sink) : {}}
              onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
              routes={routes}
              allSources={sources}
              allSinks={sinks}
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
      
      <Box
        mb={6}
        p={4}
        borderWidth="1px"
        borderRadius="lg"
        borderColor={sectionBorderColor}
        bg={sectionBg}
      >
        <Flex justify="space-between" align="center" mb={4}>
          <Flex align="center">
            <Box as="i" className="fas fa-route" mr={2} />
            <Heading as="h3" size="md" color={headingColor}>
              Active Routes
            </Heading>
          </Flex>
          <Button
            size="sm"
            onClick={() => setCurrentCategory('routes')}
            colorScheme={buttonColorScheme}
            variant="outline"
          >
            View All
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {routes.filter(route => route.enabled).slice(0, 10).map(route => (
            <ResourceCard
              key={`active-route-${route.name}`}
              item={route}
              type="routes"
              isStarred={starredRoutes.includes(route.name)}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onEqualizer={() => handleOpenRouteEqualizer(route.name)}
              onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
              routes={routes}
              allSources={sources}
              allSinks={sinks}
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

export default DashboardContent;
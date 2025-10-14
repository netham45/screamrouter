import React from 'react';
import { Box, SimpleGrid, VStack, useColorModeValue } from '@chakra-ui/react';
import { ContentProps } from '../types';
import ResourceCard from '../cards/ResourceCard';
import ResourceListItem from '../cards/ResourceListItem';
import { sortSinks } from '../utils';

/**
 * SinksContent component for the FullMenu.
 * This component displays a list of all sinks in either grid or list view.
 */
const SinksContent: React.FC<ContentProps> = ({
  sinks,
  starredSinks,
  listeningToSink,
  viewMode,
  sortConfig,
  handleStar,
  handleToggleSink,
  handleOpenSinkEqualizer,
  handleUpdateSinkVolume,
  handleUpdateSinkTimeshift,
  handleOpenVisualizer,
  actions,
  routes
}) => {
  // Sort sinks based on the current sort configuration
  const sortedSinks = sortSinks(sinks, sortConfig, starredSinks, listeningToSink);
  
  // Define colors based on color mode
  const bgColor = useColorModeValue('white', 'gray.800');

  return (
    <Box>
      {viewMode === 'grid' ? (
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {sortedSinks.map(sink => (
            <ResourceCard
              key={`sink-${sink.name}`}
              item={sink}
              type="sinks"
              isStarred={starredSinks.includes(sink.name)}
              isActive={listeningToSink?.name === sink.name}
              onStar={() => handleStar('sinks', sink.name)}
              onActivate={() => handleToggleSink(sink.name)}
              onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
              onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
              onEdit={() => actions.editItem(sink.is_group ? 'group-sink' : 'sinks', sink)}
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
              onVisualize={() => handleOpenVisualizer ? handleOpenVisualizer(sink) : undefined}
              routes={routes}
              allSinks={sinks}
              navigate={actions.navigate}
            />
          ))}
        </SimpleGrid>
      ) : (
        <VStack spacing={2} align="stretch" bg={bgColor} borderRadius="md">
          {sortedSinks.map(sink => (
            <ResourceListItem
              key={`sink-${sink.name}`}
              item={sink}
              type="sinks"
              isStarred={starredSinks.includes(sink.name)}
              isActive={listeningToSink?.name === sink.name}
              onStar={() => handleStar('sinks', sink.name)}
              onActivate={() => handleToggleSink(sink.name)}
              onEqualizer={() => handleOpenSinkEqualizer(sink.name)}
              onUpdateVolume={(volume) => handleUpdateSinkVolume(sink.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSinkTimeshift(sink.name, timeshift)}
              onEdit={() => actions.editItem(sink.is_group ? 'group-sink' : 'sinks', sink)}
              onListen={() => {
                // If this sink is already being listened to, pass null to stop listening
                if (listeningToSink?.name === sink.name) {
                  actions.listenToSink(null);
                } else {
                  // Otherwise, start listening to this sink
                  actions.listenToSink(sink);
                }
              }}
              onVisualize={() => handleOpenVisualizer ? handleOpenVisualizer(sink) : undefined}
              routes={routes}
              allSinks={sinks}
              navigate={actions.navigate}
            />
          ))}
        </VStack>
      )}
    </Box>
  );
};

export default SinksContent;
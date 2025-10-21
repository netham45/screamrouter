import React from 'react';
import { Box, SimpleGrid, VStack, useColorModeValue } from '@chakra-ui/react';
import { ContentProps } from '../types';
import ResourceCard from '../cards/ResourceCard';
import ResourceListItem from '../cards/ResourceListItem';
import { sortSources } from '../utils';

/**
 * SourcesContent component for the FullMenu.
 * This component displays a list of all sources in either grid or list view.
 */
const SourcesContent: React.FC<ContentProps> = ({
  sources,
  starredSources,
  contextActiveSource,
  viewMode,
  sortConfig,
  handleStar,
  handleToggleSource,
  handleOpenSourceEqualizer,
  handleOpenVnc,
  handleUpdateSourceVolume,
  handleUpdateSourceTimeshift,
  actions,
  routes,
  handleControlSource
}) => {
  // Filter to show only non-process sources in the Sources view
  const nonProcessSources = sources.filter(source => !source.is_process);

  // Sort sources based on the current sort configuration
  const sortedSources = sortSources(nonProcessSources, sortConfig, starredSources, contextActiveSource);
  
  // Define colors based on color mode
  const bgColor = useColorModeValue('white', 'gray.800');

  return (
    <Box>
      {viewMode === 'grid' ? (
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {sortedSources.map(source => (
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
              onUpdateVolume={(volume) => handleUpdateSourceVolume(source.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateSourceTimeshift(source.name, timeshift)}
              onEdit={() => actions.editItem(source.is_group ? 'group-source' : 'sources', source)}
              onChannelMapping={() => actions.openChannelMapping('sources', source)}
              onDelete={() => actions.deleteItem('sources', source.name)}
              onListen={() => {
                window.open(`/site/listen/source/${encodeURIComponent(source.name)}`, '_blank');
              }}
              routes={routes}
              allSources={sources}
              navigate={actions.navigate}
              onToggleActiveSource={() => actions.toggleActiveSource(source.name)}
              onControlSource={source.vnc_ip && handleControlSource ?
                (action) => handleControlSource(source.name, action) : undefined}
            />
          ))}
        </SimpleGrid>
      ) : (
        <VStack spacing={2} align="stretch" bg={bgColor} borderRadius="md">
          {sortedSources.map(source => (
            <ResourceListItem
              key={`source-${source.name}`}
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
              onEdit={() => actions.editItem(source.is_group ? 'group-source' : 'sources', source)}
              routes={routes}
              allSources={sources}
              navigate={actions.navigate}
              onToggleActiveSource={() => actions.toggleActiveSource(source.name)}
              onControlSource={source.vnc_ip && handleControlSource ?
                (action) => handleControlSource(source.name, action) : undefined}
            />
          ))}
        </VStack>
      )}
    </Box>
  );
};

export default SourcesContent;

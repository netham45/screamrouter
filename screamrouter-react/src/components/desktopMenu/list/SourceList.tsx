/**
 * Compact source list component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { Box, Table, Thead, Tbody, Tr, Th, useColorModeValue } from '@chakra-ui/react';
import { Source, Route } from '../../../api/api';
import SourceItem from '../item/SourceItem';
import { DesktopMenuActions } from '../types';

interface SourceListProps {
  /**
   * Array of sources to display
   */
  sources: Source[];
  
  /**
   * Array of routes to associate with sources
   */
  routes: Route[];
  
  /**
   * Array of starred source names
   */
  starredSources: string[];
  
  /**
   * Name of the Primary Source
   */
  activeSource: string | null;
  
  /**
   * Actions for the DesktopMenu
   */
  actions: DesktopMenuActions;
  
  /**
   * Name of the selected source
   */
  selectedItem?: string | null;
}

/**
 * A compact source list component optimized for the DesktopMenu interface.
 */
const SourceList: React.FC<SourceListProps> = ({
  sources,
  routes,
  actions,
  selectedItem
}) => {
  // Color values for light/dark mode
  const tableHeaderBg = useColorModeValue('gray.50', 'gray.700');
  
  // Get routes for a specific source
  const getSourceRoutes = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName);
  };
  
  return (
    <Box overflowX="auto" width="100%">
      <Table variant="simple" size="sm" width="100%">
        <Thead>
          <Tr>
            
            <Th bg={tableHeaderBg}>Name</Th>
            <Th bg={tableHeaderBg} width="120px">Actions</Th>
            <Th bg={tableHeaderBg} width="40px">Status</Th>
            <Th bg={tableHeaderBg} width="100px">Volume</Th>
          </Tr>
        </Thead>
        <Tbody>
          {sources.map(source => (
            <SourceItem
              key={source.name}
              source={source}
              actions={actions}
              isSelected={selectedItem === source.name}
              routes={getSourceRoutes(source.name)}
            />
          ))}
        </Tbody>
      </Table>
    </Box>
  );
};

export default SourceList;
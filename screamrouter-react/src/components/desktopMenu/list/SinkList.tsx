/**
 * Compact sink list component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { Box, Table, Thead, Tbody, Tr, Th, useColorModeValue } from '@chakra-ui/react';
import { Sink, Route } from '../../../api/api';
import SinkItem from '../item/SinkItem';
import { DesktopMenuActions } from '../types';

interface SinkListProps {
  /**
   * Array of sinks to display
   */
  sinks: Sink[];
  
  /**
   * Array of routes to associate with sinks
   */
  routes: Route[];
  
  /**
   * Array of starred sink names
   */
  starredSinks: string[];
  
  /**
   * Name of the sink being listened to
   */
  listeningToSink: string | null;
  
  /**
   * Name of the sink being visualized
   */
  visualizingSink: string | null;
  
  /**
   * Actions for the DesktopMenu
   */
  actions: DesktopMenuActions;
  
  /**
   * Name of the selected sink
   */
  selectedItem?: string | null;
}

/**
 * A compact sink list component optimized for the DesktopMenu interface.
 */
const SinkList: React.FC<SinkListProps> = ({
  sinks,
  routes,
  starredSinks,
  listeningToSink,
  visualizingSink,
  actions,
  selectedItem
}) => {
  // Color values for light/dark mode
  const tableHeaderBg = useColorModeValue('gray.50', 'gray.700');
  
  // Get routes for a specific sink
  const getSinkRoutes = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName);
  };
  
  return (
    <Box overflowX="auto" width="100%">
      <Table variant="simple" size="sm" width="100%">
        <Thead>
          <Tr>
            <Th bg={tableHeaderBg}>Name</Th>
            <Th bg={tableHeaderBg} width="40px">Status</Th>
            <Th bg={tableHeaderBg} width="100px">Volume</Th>
          </Tr>
        </Thead>
        <Tbody>
          {sinks.map(sink => (
            <SinkItem
              key={sink.name}
              sink={sink}
              routes={getSinkRoutes(sink.name)}
              isStarred={starredSinks.includes(sink.name)}
              listeningToSink={listeningToSink}
              visualizingSink={visualizingSink}
              actions={actions}
              isSelected={selectedItem === sink.name}
            />
          ))}
        </Tbody>
      </Table>
    </Box>
  );
};

export default SinkList;
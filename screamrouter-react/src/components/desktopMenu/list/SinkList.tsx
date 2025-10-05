/**
 * Compact sink list component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React, { useState } from 'react';
import { Box, Table, Thead, Tbody, Tr, Th, Icon, Flex } from '@chakra-ui/react';
import { TriangleDownIcon, TriangleUpIcon } from '@chakra-ui/icons';
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
   * Map of sink names to their listening status
   */
  listeningStatus: Map<string, boolean>;
  
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

// Type for sort direction
type SortDirection = 'asc' | 'desc' | 'none';

// Type for sort field
type SortField = 'name' | 'status' | 'volume';

/**
 * A compact sink list component optimized for the DesktopMenu interface.
 */
const SinkList: React.FC<SinkListProps> = ({
  sinks,
  routes,
  starredSinks,
  listeningStatus,
  visualizingSink,
  actions,
  selectedItem
}) => {
  const tableHeaderBg = "transparent";//getDarkerColor(.75, 1);
  
  // State for sorting with localStorage persistence
  const [sortField, setSortField] = useState<SortField | null>(() => {
    const saved = localStorage.getItem('sinkList_sortField');
    return saved ? saved as SortField : null;
  });
  
  const [sortDirection, setSortDirection] = useState<SortDirection>(() => {
    const saved = localStorage.getItem('sinkList_sortDirection');
    return saved ? saved as SortDirection : 'none';
  });
  
  // Get routes for a specific sink
  const getSinkRoutes = (sinkName: string) => {
    return routes.filter(route => route.sink === sinkName);
  };
  
  // Handle header click for sorting
  const handleHeaderClick = (field: SortField) => {
    let newDirection: SortDirection = 'asc';
    let newField: SortField | null = field;
    
    if (sortField === field) {
      // Cycle through sort directions: asc -> desc -> none
      if (sortDirection === 'asc') {
        newDirection = 'desc';
      } else if (sortDirection === 'desc') {
        newDirection = 'none';
        newField = null;
      } else {
        newDirection = 'asc';
      }
    }
    
    // Update state
    setSortField(newField);
    setSortDirection(newDirection);
    
    // Save to localStorage
    if (newField) {
      localStorage.setItem('sinkList_sortField', newField);
    } else {
      localStorage.removeItem('sinkList_sortField');
    }
    localStorage.setItem('sinkList_sortDirection', newDirection);
  };
  
  // Sort the sinks based on current sort field and direction
  const sortedSinks = [...sinks].sort((a, b) => {
    if (sortField === null || sortDirection === 'none') {
      return 0; // No sorting
    }
    
    let comparison = 0;
    
    if (sortField === 'name') {
      comparison = a.name.localeCompare(b.name);
    } else if (sortField === 'status') {
      // Sort by the enabled property
      const aEnabled = a.enabled;
      const bEnabled = b.enabled;
      comparison = (aEnabled === bEnabled) ? 0 : aEnabled ? -1 : 1;
    } else if (sortField === 'volume') {
      comparison = a.volume - b.volume;
    }
    
    return sortDirection === 'asc' ? comparison : -comparison;
  });
  
  return (
    <Box overflowX="auto" width="100%">
      <Table variant="simple" size="sm" width="100%">
        <Thead sx={{"th": {"borderBottom": '1px', "borderColor": "rgba(0,0,0,.1)"}}}>
          <Tr sx={{th: {color:"#EEEEEE"}}}>
            <Th 
              bg={tableHeaderBg} 
              cursor="pointer" 
              onClick={() => handleHeaderClick('name')}
            >
              <Flex align="center">
                Name
                {sortField === 'name' && sortDirection !== 'none' && (
                  <Icon 
                    as={sortDirection === 'asc' ? TriangleUpIcon : TriangleDownIcon} 
                    ml={1} 
                    boxSize={3} 
                  />
                )}
              </Flex>
            </Th>
            <Th 
              bg={tableHeaderBg} 
              width="40px" 
              cursor="pointer" 
              onClick={() => handleHeaderClick('status')}
            >
              <Flex align="center">
                Status
                {sortField === 'status' && sortDirection !== 'none' && (
                  <Icon 
                    as={sortDirection === 'asc' ? TriangleUpIcon : TriangleDownIcon} 
                    ml={1} 
                    boxSize={3} 
                  />
                )}
              </Flex>
            </Th>
            <Th 
              bg={tableHeaderBg} 
              width="100px" 
              cursor="pointer" 
              onClick={() => handleHeaderClick('volume')}
            >
              <Flex align="center">
                Volume
                {sortField === 'volume' && sortDirection !== 'none' && (
                  <Icon 
                    as={sortDirection === 'asc' ? TriangleUpIcon : TriangleDownIcon} 
                    ml={1} 
                    boxSize={3} 
                  />
                )}
              </Flex>
            </Th>
          </Tr>
        </Thead>
        <Tbody>
          {sortedSinks.map(sink => (
            <SinkItem
              key={sink.name}
              sink={sink}
              routes={getSinkRoutes(sink.name)}
              isStarred={starredSinks.includes(sink.name)}
              isListening={listeningStatus.get(sink.name) || false}
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

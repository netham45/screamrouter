/**
 * Compact source list component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React, { useState } from 'react';
import { Box, Table, Thead, Tbody, Tr, Th, Icon, Flex } from '@chakra-ui/react';
import { TriangleDownIcon, TriangleUpIcon } from '@chakra-ui/icons';
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
  /**
   * Show processes?
   */
  showProcesses?: boolean;
}

// Type for sort direction
type SortDirection = 'asc' | 'desc' | 'none';

// Type for sort field
type SortField = 'name' | 'status' | 'volume';

/**
 * A compact source list component optimized for the DesktopMenu interface.
 */
const SourceList: React.FC<SourceListProps> = ({
  sources,
  routes,
  starredSources,
  activeSource,
  actions,
  selectedItem,
  showProcesses=false,
}) => {
  // Get colors from context
  //const { getDarkerColor } = useColorContext();
  const tableHeaderBg = "transparent";//getDarkerColor(.75, 1);
  
  // State for sorting with localStorage persistence
  const [sortField, setSortField] = useState<SortField | null>(() => {
    const saved = localStorage.getItem('sourceList_sortField');
    return saved ? saved as SortField : null;
  });
  
  const [sortDirection, setSortDirection] = useState<SortDirection>(() => {
    const saved = localStorage.getItem('sourceList_sortDirection');
    return saved ? saved as SortDirection : 'none';
  });
  
  // Get routes for a specific source
  const getSourceRoutes = (sourceName: string) => {
    return routes.filter(route => route.source === sourceName);
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
      localStorage.setItem('sourceList_sortField', newField);
    } else {
      localStorage.removeItem('sourceList_sortField');
    }
    localStorage.setItem('sourceList_sortDirection', newDirection);
  };
  
  // Update sources to mark the primary source
  const updatedSources = sources.map(source => ({
    ...source,
    is_primary: source.name === activeSource
  }));
  
  // Sort the sources based on current sort field and direction
  let sortedSources = [...updatedSources].sort((a, b) => {
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

  if (!showProcesses) {
    sortedSources = sortedSources.filter((source)=>{return !source.is_process;});
  }
  
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
            <Th bg={tableHeaderBg} width="120px">Actions</Th>
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
          {sortedSources.map(source => (
            <SourceItem
              key={source.name}
              source={source}
              isStarred={starredSources.includes(source.name)}
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

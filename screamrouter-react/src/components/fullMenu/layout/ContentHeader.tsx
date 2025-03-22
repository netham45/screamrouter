import React from 'react';
import {
  Flex,
  HStack,
  Breadcrumb,
  BreadcrumbItem,
  BreadcrumbLink,
  Button,
  useColorModeValue
} from '@chakra-ui/react';
import { ContentHeaderProps } from '../types';
import { getCategoryTitle, openAddPage } from '../utils';
import { ViewModeToggle, SortDropdown } from '../controls';

/**
 * ContentHeader component for the FullMenu.
 * This component displays the breadcrumbs and view controls for the content panel.
 * Uses Chakra UI components for consistent styling.
 */
const ContentHeader: React.FC<ContentHeaderProps> = ({
  currentCategory,
  viewMode,
  setViewMode,
  sortConfig,
  onSort
}) => {
  // Define colors based on color mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  const showViewControls = ['sources', 'sinks', 'routes'].includes(currentCategory);
  const showAddButton = ['sources', 'sinks', 'routes'].includes(currentCategory);
  
  // Function to handle adding a new item
  const handleAddItem = () => {
    switch (currentCategory) {
      case 'sources':
        openAddPage('sources');
        break;
      case 'sinks':
        openAddPage('sinks');
        break;
      case 'routes':
        openAddPage('routes');
        break;
      default:
        break;
    }
  };
  
  // Function to handle adding a new group
  const handleAddGroup = () => {
    switch (currentCategory) {
      case 'sources':
        openAddPage('group-source');
        break;
      case 'sinks':
        openAddPage('group-sink');
        break;
      default:
        break;
    }
  };

  // Sort options for the SortDropdown component
  const sortOptions = [
    { key: 'name', label: 'Name' },
    { key: 'enabled', label: 'Status' },
    ...(currentCategory !== 'routes' ? [{ key: 'active', label: 'Active' }] : []),
    { key: 'favorite', label: 'Favorite' }
  ];

  return (
    <Flex
      p={4}
      bg={bgColor}
      borderBottomWidth="1px"
      borderColor={borderColor}
      justify="space-between"
      align="center"
      flexWrap={['wrap', 'nowrap']}
    >
      <Flex align="center">
        <Breadcrumb separator="/" mb={[2, 0]}>
          <BreadcrumbItem>
            <BreadcrumbLink>ScreamRouter</BreadcrumbLink>
          </BreadcrumbItem>
          <BreadcrumbItem isCurrentPage>
            <BreadcrumbLink fontWeight="semibold">
              {getCategoryTitle(currentCategory)}
            </BreadcrumbLink>
          </BreadcrumbItem>
        </Breadcrumb>
        
        {showAddButton && (
          <HStack spacing={2} ml={4}>
            <Button
              size="sm"
              colorScheme="blue"
              leftIcon={<i className="fas fa-plus"></i>}
              onClick={handleAddItem}
            >
              Add {currentCategory === 'sources' ? 'Source' : currentCategory === 'sinks' ? 'Sink' : 'Route'}
            </Button>
            
            {(currentCategory === 'sources' || currentCategory === 'sinks') && (
              <Button
                size="sm"
                colorScheme="purple"
                leftIcon={<i className="fas fa-object-group"></i>}
                onClick={handleAddGroup}
              >
                Add {currentCategory === 'sources' ? 'Source' : 'Sink'} Group
              </Button>
            )}
          </HStack>
        )}
      </Flex>
      
      {showViewControls && (
        <HStack spacing={4} width={['100%', 'auto']}>
          <ViewModeToggle 
            viewMode={viewMode} 
            onChange={setViewMode} 
          />
          
          <SortDropdown 
            sortConfig={sortConfig} 
            onSort={onSort} 
            options={sortOptions}
            label="Sort by"
          />
        </HStack>
      )}
    </Flex>
  );
};

export default ContentHeader;
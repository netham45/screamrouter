import React from 'react';
import {
  Box,
  Flex,
  VStack,
  Heading,
  Divider,
  useColorModeValue,
  CloseButton,
} from '@chakra-ui/react';
import NavItem from '../navigation/NavItem';
import { SidebarProps } from '../types';

/**
 * Sidebar component for the FullMenu.
 * This component displays navigation items for different content categories.
 * Uses Chakra UI components for consistent styling.
 */
const Sidebar: React.FC<SidebarProps> = ({
  currentCategory,
  setCurrentCategory,
  sources,
  sinks,
  routes,
  starredSources,
  starredSinks,
  starredRoutes,
  sidebarOpen,
  toggleSidebar,
}) => {
  // Define colors based on color mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const headingColor = useColorModeValue('gray.700', 'gray.100');
  const sectionHeadingColor = useColorModeValue('gray.500', 'gray.400');

  return (
    <>
      <Box
        as="aside"
        position={{ base: 'fixed', md: 'relative' }}
        left={{ base: 0, md: 'auto' }}
        top={{ base: 0, md: 'auto' }}
        bottom={{ base: 0, md: 'auto' }}
        w={['100%', '100%', '250px']}
        bg={bgColor}
        borderRightWidth="1px"
        borderColor={borderColor}
        transform={{
          base: sidebarOpen ? 'translateX(0)' : 'translateX(-100%)',
          md: 'translateX(0)'
        }}
        transition="transform 0.3s ease"
        zIndex={20}
        display="flex"
        flexDirection="column"
        overflowY="auto"
        height={{ md: '100%' }}
      >
        <Flex
          p={4}
          justify="space-between"
          align="center"
          borderBottomWidth="1px"
          borderColor={borderColor}
        >
          <Heading as="h2" size="md" color={headingColor}>
            Navigation
          </Heading>
          <CloseButton
            display={{ base: 'block', md: 'none' }}
            onClick={toggleSidebar}
          />
        </Flex>
        
        <VStack
          as="nav"
          spacing={6}
          align="stretch"
          flex="1"
          p={4}
          overflowY="auto"
        >
          <Box>
            <Heading
              as="h3"
              size="sm"
              mb={2}
              color={sectionHeadingColor}
              fontWeight="medium"
            >
              Overview
            </Heading>
            <VStack spacing={1} align="stretch">
              <NavItem
                icon="dashboard"
                label="Dashboard"
                isActive={currentCategory === 'dashboard'}
                onClick={() => {
                  setCurrentCategory('dashboard');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
              <NavItem
                icon="broadcast-tower"
                label="Primary Source"
                isActive={currentCategory === 'active-source'}
                onClick={() => {
                  setCurrentCategory('active-source');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
              <NavItem
                icon="headphones"
                label="Now Listening"
                isActive={currentCategory === 'now-listening'}
                onClick={() => {
                  setCurrentCategory('now-listening');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
               <NavItem
               icon="chart-bar"
               label="Stats"
               isActive={currentCategory === 'stats'}
               onClick={() => {
                 setCurrentCategory('stats');
                 if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
               }}
               />
            </VStack>
          </Box>
          
          <Divider />
          
          <Box>
            <Heading
              as="h3"
              size="sm"
              mb={2}
              color={sectionHeadingColor}
              fontWeight="medium"
            >
              Resources
            </Heading>
            <VStack spacing={1} align="stretch">
              <NavItem
                icon="microphone"
                label="Sources"
                isActive={currentCategory === 'sources'}
                badge={sources.length}
                onClick={() => {
                  setCurrentCategory('sources');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
              <NavItem
                icon="volume-up"
                label="Sinks"
                isActive={currentCategory === 'sinks'}
                badge={sinks.length}
                onClick={() => {
                  setCurrentCategory('sinks');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
              <NavItem
                icon="route"
                label="Routes"
                isActive={currentCategory === 'routes'}
                badge={routes.length}
                onClick={() => {
                  setCurrentCategory('routes');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
            </VStack>
          </Box>
          
          <Divider />
          
          <Box>
            <Heading
              as="h3"
              size="sm"
              mb={2}
              color={sectionHeadingColor}
              fontWeight="medium"
            >
              Favorites
            </Heading>
            <VStack spacing={1} align="stretch">
              <NavItem
                icon="star"
                label="Starred Items"
                isActive={currentCategory === 'favorites'}
                badge={starredSources.length + starredSinks.length + starredRoutes.length}
                onClick={() => {
                  setCurrentCategory('favorites');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
            </VStack>
          </Box>
        </VStack>
        
      </Box>
      
      {/* Sidebar Overlay (for mobile) */}
      {sidebarOpen && (
        <Box
          position="fixed"
          top={0}
          left={0}
          right={0}
          bottom={0}
          bg="blackAlpha.600"
          zIndex={10}
          display={{ base: 'block', md: 'none' }}
          onClick={toggleSidebar}
        />
      )}
    </>
  );
};

export default Sidebar;
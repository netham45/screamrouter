/**
 * Main DesktopMenu component.
 * This is a specialized component for the slide-out panel interface.
 */
import React, { useState, useEffect, useMemo } from 'react';
import { Box, Flex, ButtonGroup, Button, Alert, AlertIcon, useColorModeValue } from '@chakra-ui/react';
import { useAppContext } from '../../context/AppContext';
import SourceList from './list/SourceList';
import SinkList from './list/SinkList';
import RouteList from './list/RouteList';
import { MenuLevel, DesktopMenuActions } from './types';
import { createDesktopMenuActions } from './utils';

/**
 * The main DesktopMenu component optimized for the slide-out panel interface.
 */
const DesktopMenu: React.FC = () => {
  // Get context data and functions
  const {
    sources,
    sinks,
    routes,
    activeSource,
    listeningToSink,
    visualizingSink,
    onListenToSink,
    onVisualizeSink,
    onToggleActiveSource,
  } = useAppContext();
  
  // State for menu navigation
  const [currentMenu, setCurrentMenu] = useState<MenuLevel>(() => {
    const savedMenu = localStorage.getItem('currentMenu');
    return savedMenu ? parseInt(savedMenu, 10) : MenuLevel.Main;
  });
  
  // State for starred items
  const [starredSources, setStarredSources] = useState<string[]>([]);
  const [starredSinks, setStarredSinks] = useState<string[]>([]);
  const [starredRoutes, setStarredRoutes] = useState<string[]>([]);
  
  // State for selected item
  const [selectedItem, setSelectedItem] = useState<string | null>(null);
  
  // State for error messages
  const [error, setError] = useState<string | null>(null);
  
  // Reference to content area for scrolling
  const contentRef = React.useRef<HTMLDivElement>(null);
  
  // Color values for light/dark mode
  const bgColor = useColorModeValue('rgba(255,255,255,.95)', 'rgba(39,39,42,.95)');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const buttonBgActive = useColorModeValue('blue.500', 'blue.300');
  const buttonBgInactive = useColorModeValue('gray.100', 'gray.700');
  const buttonTextActive = useColorModeValue('white', 'gray.900');
  const buttonTextInactive = useColorModeValue('gray.800', 'white');
  
  // Load starred items from localStorage
  useEffect(() => {
    const starredSourcesData = JSON.parse(localStorage.getItem('starredSources') || '[]');
    const starredSinksData = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    const starredRoutesData = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
    setStarredSources(starredSourcesData);
    setStarredSinks(starredSinksData);
    setStarredRoutes(starredRoutesData);
  }, []);
  
  // Save current menu to localStorage
  useEffect(() => {
    localStorage.setItem('currentMenu', currentMenu.toString());
  }, [currentMenu]);
  
  // Scroll to selected item when it changes
  useEffect(() => {
    if (selectedItem && contentRef.current) {
      const element = document.getElementById(`${MenuLevel[currentMenu].toLowerCase()}-${selectedItem}`);
      if (element) {
        element.scrollIntoView({ behavior: 'smooth', block: 'center' });
      }
    }
  }, [selectedItem, currentMenu]);
  
  // This function is used by the createDesktopMenuActions utility
  // It's kept here for reference but the actual implementation is in utils.ts
  /*
  const openInNewWindow = (url: string, width: number = 800, height: number = 600) => {
    const left = (window.screen.width - width) / 2;
    const top = (window.screen.height - height) / 2;
    window.open(url, '_blank', `width=${width},height=${height},left=${left},top=${top}`);
  };
  */
  
  // Function to navigate to a specific item
  const navigateToItem = (type: 'sources' | 'sinks' | 'routes', itemName: string) => {
    let menuLevel: MenuLevel;
    
    switch (type) {
      case 'sources':
        menuLevel = MenuLevel.AllSources;
        break;
      case 'sinks':
        menuLevel = MenuLevel.AllSinks;
        break;
      case 'routes':
        menuLevel = MenuLevel.AllRoutes;
        break;
      default:
        menuLevel = MenuLevel.Main;
    }
    
    setCurrentMenu(menuLevel);
    setSelectedItem(itemName);
  };
  
  // Function to set starred items
  const setStarredItemsHandler = (type: 'sources' | 'sinks' | 'routes', setter: (prev: string[]) => string[]) => {
    if (type === 'sources') setStarredSources(setter);
    else if (type === 'sinks') setStarredSinks(setter);
    else if (type === 'routes') setStarredRoutes(setter);
  };
  
  // Create actions for the DesktopMenu
  const actions: DesktopMenuActions = useMemo(() => createDesktopMenuActions(
    setStarredItemsHandler,
    setError,
    onToggleActiveSource,
    // Type conversion for the sink-related callbacks
    (name: string | null) => onListenToSink(name ? sinks.find(s => s.name === name) || null : null),
    (name: string | null) => onVisualizeSink(name ? sinks.find(s => s.name === name) || null : null),
    navigateToItem
  ), [onToggleActiveSource, onListenToSink, onVisualizeSink, sinks]);
  
  // Function to open the full interface
  const openFullInterface = () => {
    window.open('/', '_blank');
  };
  
  // Function to render content based on current menu
  const renderContent = () => {
    const primarySource = sources.find(source => source.name === activeSource);
    
    switch (currentMenu) {
      case MenuLevel.Sources:
        return (
          <SourceList
            sources={sources.filter(source => starredSources.includes(source.name))}
            routes={routes}
            starredSources={starredSources}
            activeSource={activeSource}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.Sinks:
        return (
          <SinkList
            sinks={sinks.filter(sink => starredSinks.includes(sink.name))}
            routes={routes}
            starredSinks={starredSinks}
            listeningToSink={listeningToSink?.name || null}
            visualizingSink={visualizingSink?.name || null}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.Routes:
        return (
          <RouteList
            routes={routes.filter(route => starredRoutes.includes(route.name))}
            starredRoutes={starredRoutes}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.AllSources:
        return (
          <SourceList
            sources={sources}
            routes={routes}
            starredSources={starredSources}
            activeSource={activeSource}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.AllSinks:
        return (
          <SinkList
            sinks={sinks}
            routes={routes}
            starredSinks={starredSinks}
            listeningToSink={listeningToSink?.name || null}
            visualizingSink={visualizingSink?.name || null}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.AllRoutes:
        return (
          <RouteList
            routes={routes}
            starredRoutes={starredRoutes}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.NowListening:
        return (
          <SinkList
            sinks={listeningToSink ? [listeningToSink] : []}
            routes={routes}
            starredSinks={starredSinks}
            listeningToSink={listeningToSink?.name || null}
            visualizingSink={visualizingSink?.name || null}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      default: // MenuLevel.Main
        return (
          <SourceList
            sources={primarySource ? [primarySource] : []}
            routes={routes}
            starredSources={starredSources}
            activeSource={activeSource}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
    }
  };
  
  return (
    <Flex direction="column" height="610px" maxHeight="610px" justifyContent="flex-end" alignContent="flex-end">
      <Box
        bg={bgColor}
        borderWidth="1px"
        borderColor={borderColor}
        borderRadius="md"
        p={2}
        width="100%"
        maxH="610px"
        display="flex"
        flexDirection="column"
        className="desktop-menu"
        justifySelf="flex-end"
      >
        {error && (
          <Alert status="error" mb={2} borderRadius="md" size="sm">
            <AlertIcon />
            {error}
          </Alert>
        )}
        
        
        
        <Box
          flex="1"
          overflow="auto"
          ref={contentRef}
          borderWidth="1px"
          borderColor={borderColor}
          borderRadius="md"
          p={1}
        >
          {renderContent()}
        </Box>
        <Flex
          as="nav"
          p={1}
          bg={borderColor}
          borderRadius="md"
          wrap="wrap"
          gap={1}
        >
          <ButtonGroup variant="outline" isAttached spacing={0} size="sm">
            <Button
              bg={currentMenu === MenuLevel.Main ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.Main ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.Main)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              Primary
            </Button>
            <Button
              bg={currentMenu === MenuLevel.Sources ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.Sources ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.Sources)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              ★ Sources
            </Button>
            <Button
              bg={currentMenu === MenuLevel.Sinks ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.Sinks ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.Sinks)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              ★ Sinks
            </Button>
            <Button
              bg={currentMenu === MenuLevel.Routes ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.Routes ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.Routes)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              ★ Routes
            </Button>
            <Button
              bg={currentMenu === MenuLevel.AllSources ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.AllSources ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.AllSources)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              All Sources
            </Button>
            <Button
              bg={currentMenu === MenuLevel.AllSinks ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.AllSinks ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.AllSinks)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              All Sinks
            </Button>
            <Button
              bg={currentMenu === MenuLevel.AllRoutes ? buttonBgActive : buttonBgInactive}
              color={currentMenu === MenuLevel.AllRoutes ? buttonTextActive : buttonTextInactive}
              onClick={() => setCurrentMenu(MenuLevel.AllRoutes)}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              All Routes
            </Button>
            <Button
              onClick={openFullInterface}
              _hover={{ opacity: 0.8 }}
              size="xs"
            >
              Full View
            </Button>
          </ButtonGroup>
        </Flex>
      </Box>
    </Flex>
  );
};

export default DesktopMenu;
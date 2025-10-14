/**
 * Main DesktopMenu component.
 * This is a specialized component for the slide-out panel interface.
 */
import React, { useState, useEffect, useMemo } from 'react';
import { Box, Flex, ButtonGroup, Button, Alert, AlertIcon, HStack } from '@chakra-ui/react';
import ConfirmationDialog from '../dialogs/ConfirmationDialog';
import ApiService, { Source, Sink, Route } from '../../api/api'; // Ensure SpeakerLayout and item types are imported
import { colorContextInstance } from './context/ColorContext';
import { useAppContext } from '../../context/AppContext';
// SpeakerLayoutPage is now opened in a new window, so direct import/use here is removed.
import SourceList from './list/SourceList';
import SinkList from './list/SinkList';
import RouteList from './list/RouteList';
import { getRecents } from '../../utils/recents';
import { MenuLevel, DesktopMenuActions } from './types';
import { Heading } from '@chakra-ui/react'; // Removed Text
import { createDesktopMenuActions } from './utils';
import AddMenuDropdown from './controls/AddMenuDropdown';

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
    listeningStatus,
    visualizingSink,
    onTranscribeSink,
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
  
  // State for delete confirmation dialog
  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [deleteItemType, setDeleteItemType] = useState<'sources' | 'sinks' | 'routes' | null>(null);
  const [deleteItemName, setDeleteItemName] = useState<string | null>(null);

  // Reference to content area for scrolling
  const contentRef = React.useRef<HTMLDivElement>(null);
  
  // Track color changes
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [_, setColorUpdate] = useState(0);
  useEffect(() => {
    return colorContextInstance.subscribe(() => {
      setColorUpdate(n => n + 1);
    });
  }, []);
  
  // Get colors from the global singleton
  const bgColor = colorContextInstance.getDarkerColor(.9, .9);
  const borderColor = colorContextInstance.getDarkerColor(.88, .9); // Subtle border
  const buttonBgActive = colorContextInstance.getDarkerColor(.7); // Full color for active
  const buttonBgInactive = colorContextInstance.getDarkerColor(0.5); // Darker for inactive
  const buttonTextActive = '#EEEEEE'; // White text on colored background
  const buttonTextInactive = '#EEE'; // Gray text for inactive
  
  // Load starred items from localStorage - refresh when activeSource changes
  useEffect(() => {
    const starredSourcesData: string[] = JSON.parse(localStorage.getItem('starredSources') || '[]');
    const starredSinksData: string[] = JSON.parse(localStorage.getItem('starredSinks') || '[]');
    const starredRoutesData: string[] = JSON.parse(localStorage.getItem('starredRoutes') || '[]');
    setStarredSources(starredSourcesData);
    setStarredSinks(starredSinksData);
    setStarredRoutes(starredRoutesData);
  }, [activeSource]); // Add activeSource as dependency so the component refreshes when it changes
  
  // Listen for changes to localStorage to refresh when favorites change
  useEffect(() => {
    const handleStorageChange = (event: StorageEvent) => {
      if (event.key === 'starredSources' || event.key === 'starredSinks' || event.key === 'starredRoutes') {
        const key = event.key;
        const newValue = JSON.parse(event.newValue || '[]');
        if (key === 'starredSources') {
          setStarredSources(newValue);
        } else if (key === 'starredSinks') {
          setStarredSinks(newValue);
        } else if (key === 'starredRoutes') {
          setStarredRoutes(newValue);
        }
      }
    };
    
    window.addEventListener('storage', handleStorageChange);
    return () => {
      window.removeEventListener('storage', handleStorageChange);
    };
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
    (ip: string) => onTranscribeSink(ip),
    (name: string | null) => { if (name) onListenToSink(name); },
    (name: string | null) => onVisualizeSink(name ? sinks.find(s => s.name === name) || null : null),
    navigateToItem
    // openDeleteDialog and showSpeakerLayoutPage will be assigned to actions object later
  ), [onToggleActiveSource, onTranscribeSink, onListenToSink, onVisualizeSink, sinks, navigateToItem]);
  
  const showSpeakerLayoutPage = (type: 'sources' | 'sinks' | 'routes', item: Source | Sink | Route) => {
    try {
      const encodedName = encodeURIComponent(item.name);
      const url = `/site/speaker-layout-standalone?type=${type}&name=${encodedName}`;
      
      const windowFeatures = 'width=650,height=750,menubar=no,toolbar=no,location=no,resizable=yes,scrollbars=yes,status=no';
      const newWindow = window.open(url, '_blank', windowFeatures);

      if (newWindow) {
        newWindow.focus();
      } else {
        setError('Failed to open new window. Please check your browser pop-up blocker settings.');
      }
    } catch (e) {
      console.error("Error opening speaker layout window:", e);
      setError('Could not open speaker layout page. See console for details.');
    }
  };

  // Add the actual showSpeakerLayoutPage to the actions object after its definition
  actions.showSpeakerLayoutPage = showSpeakerLayoutPage;
  
  // Function to open the full interface
  const openFullInterface = () => {
     window.open('/site/', 'FullView');
  };

  // Function to open a URL in a new window (similar to the pattern used elsewhere)
  const openInNewWindow = (url: string, width: number = 800, height: number = 700) => {
    const left = (window.screen.width - width) / 2;
    const top = (window.screen.height - height) / 2;
    const windowFeatures = `width=${width},height=${height},left=${left},top=${top},menubar=no,toolbar=no,location=no,resizable=yes,scrollbars=yes,status=no`;
    const newWindow = window.open(url, '_blank', windowFeatures);

    if (newWindow) {
      newWindow.focus();
    } else {
      setError('Failed to open new window. Please check your browser pop-up blocker settings.');
    }
  };

  // Functions to open add pages
  const handleAddSource = () => {
    openInNewWindow('/site/add-source');
  };

  const handleAddSourceGroup = () => {
    openInNewWindow('/site/add-group?type=source');
  };

  const handleAddSink = () => {
    openInNewWindow('/site/add-sink');
  };

  const handleAddSinkGroup = () => {
    openInNewWindow('/site/add-group?type=sink');
  };

  const handleAddRoute = () => {
    openInNewWindow('/site/add-route');
  };
  
  const getRecentRoutes = () => {
    const appContext = useAppContext();
    const recentNames = getRecents('routes');
    return (appContext.routes || []).filter(r => recentNames.includes(r.name));
  }

  const getRecentSinks = () => {
    const appContext = useAppContext();
    const recentNames = getRecents('sinks');
    return (appContext.sinks || []).filter(r => recentNames.includes(r.name));
  }

  const getRecentSources = () => {
    const appContext = useAppContext();
    const recentNames = getRecents('sources');
    return (appContext.sources || []).filter(r => recentNames.includes(r.name));
  }

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
            listeningStatus={listeningStatus}
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
            listeningStatus={listeningStatus}
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
            sinks={sinks.filter(s => listeningStatus.get(s.name))}
            routes={routes}
            starredSinks={starredSinks}
            listeningStatus={listeningStatus}
            visualizingSink={visualizingSink?.name || null}
            actions={actions}
            selectedItem={selectedItem}
          />
        );
      case MenuLevel.RecentlyUsed:
        return (
          <Box pl={2} pt={2}>
            <Heading size="md" mb={3}>Recently Used</Heading>
            <Heading size="x-sm" mb={3}>Sources</Heading>
            <SourceList
              sources={getRecentSources()}
              routes={routes}
              starredSources={starredSources}
              activeSource={activeSource}
              actions={actions}
              selectedItem={selectedItem}
            />
            
            
            <Heading size="x-sm" mt={5} mb={3}>Sinks</Heading>
            <SinkList
              routes={routes}
              sinks={getRecentSinks()}
              listeningStatus={listeningStatus}
              visualizingSink={visualizingSink?.name || null}
              starredSinks={starredSinks}
              actions={actions}
              selectedItem={selectedItem}
            />
            
            <Heading size="x-sm" mt={5} mb={3}>Routes</Heading>
            <RouteList
              routes={getRecentRoutes()}
              starredRoutes={starredRoutes}
              actions={actions}
              selectedItem={selectedItem}
            />
            {}
          </Box>
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
  
  // Function to handle delete confirmation
  const handleConfirmDelete = async () => {
    if (!deleteItemType || !deleteItemName) return;
    
    try {
      if (deleteItemType === 'sources') {
        await ApiService.deleteSource(deleteItemName);
      } else if (deleteItemType === 'sinks') {
        await ApiService.deleteSink(deleteItemName);
      } else if (deleteItemType === 'routes') {
        await ApiService.deleteRoute(deleteItemName);
      }
      
      // Remove from starred items if it was starred
      if (deleteItemType === 'sources' && starredSources.includes(deleteItemName)) {
        setStarredItemsHandler('sources', prev => prev.filter(item => item !== deleteItemName));
      } else if (deleteItemType === 'sinks' && starredSinks.includes(deleteItemName)) {
        setStarredItemsHandler('sinks', prev => prev.filter(item => item !== deleteItemName));
      } else if (deleteItemType === 'routes' && starredRoutes.includes(deleteItemName)) {
        setStarredItemsHandler('routes', prev => prev.filter(item => item !== deleteItemName));
      }
    } catch (error) {
      console.error(`Error deleting ${deleteItemType}:`, error);
      setError(`Error deleting ${deleteItemType}`);
    } finally {
      // Reset dialog state
      setDeleteDialogOpen(false);
      setDeleteItemType(null);
      setDeleteItemName(null);
    }
  };
  
  // Function to open delete confirmation dialog
  const openDeleteDialog = (type: 'sources' | 'sinks' | 'routes', name: string) => {
    setDeleteItemType(type);
    setDeleteItemName(name);
    setDeleteDialogOpen(true);
  };
  
  // Function to handle clicks on the desktop menu when alert is open
  const handleDesktopMenuClick = (e: React.MouseEvent) => {
    // Only close the dialog if the desktop menu is clicked (not the dialog itself)
    if (deleteDialogOpen) {
      // Check if the click is on the desktop menu but not on the dialog
      const dialogElement = document.querySelector('[role="alertdialog"]');
      if (dialogElement && !dialogElement.contains(e.target as Node)) {
        setDeleteDialogOpen(false);
        // Stop propagation to prevent the click from affecting other elements
        e.stopPropagation();
      }
    }
  };
  
  // Override the confirmDelete action with our local implementation
  actions.confirmDelete = openDeleteDialog;
  // The showSpeakerLayoutPage is now correctly part of actions object above
  
  return (
    <Flex direction="column" height="950px" maxHeight="950px" justifyContent="flex-end" alignContent="flex-end">
      {/* Delete Confirmation Dialog */}
      <ConfirmationDialog
        isOpen={deleteDialogOpen}
        onClose={() => setDeleteDialogOpen(false)}
        onConfirm={handleConfirmDelete}
        title="Delete Item"
        message={`Are you sure you want to delete ${deleteItemName}? This action cannot be undone.`}
      />

      {/* Speaker Layout Page is now opened in a new window */}

      <Box
        onClick={handleDesktopMenuClick}
        style={{
          backgroundColor: "rgba(0,0,0,0);",
          borderWidth: "0px",
          borderColor: borderColor,
          borderStyle: "solid",
          borderRadius: "4px",
          color: "#EEEEEE",
          fontWeight: "lighter",
          fontFamily: "Segoe UI"
        }}
        pb={3}
        pr={3}
        marginLeft="30%"
        width="70%"
        maxW="70%"
        maxH="950px"
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
          style={{
            borderWidth: "1px",
            borderStyle: "solid",
            borderColor: "rgba(128, 128, 128, .5)",
            borderTopWidth: "1px",
            borderLeftWidth: "1px",
            borderRightWidth: "1px",
            borderBottomWidth: "0px",
            borderTopLeftRadius: "8px",
            borderTopRightRadius: "8px",
            borderBottomLeftRadius: "0px",
            borderBottomRightRadius: "0px",
            padding: "4px"
          }}
          sx={{
            'td': {
              "padding-bottom": "0",
              "padding-top": "0",
              "border": "0",
            },
          }}
          backgroundColor={bgColor}
        >
          {renderContent()}
        </Box>
        <Flex
          as="nav"
          p={1}
          style={{
            backgroundColor: borderColor,
            border: "solid",
            borderColor: "rgba(128, 128, 128, .5)",
            borderTopLeftRadius: "0px",
            borderTopRightRadius: "0px",
            borderBottomLeftRadius: "8px",
            borderBottomRightRadius: "8px",
            borderTopWidth: "0px",
            borderLeftWidth: "1px",
            borderRightWidth: "1px",
            borderBottomWidth: "1px",
            padding: "4px"
          }}
          wrap="wrap"
          gap={1}
          width="100%"
        >
          {/* First Row - Starred/Primary buttons */}
          <HStack width="100%" justify="center" mb={1}>
            <ButtonGroup variant="outline" isAttached spacing={0} size="sm">
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.Main ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.Main ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.Main)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                Primary
              </Button>
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.Sources ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.Sources ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.Sources)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                ★ Sources
              </Button>
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.Sinks ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.Sinks ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.Sinks)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                ★ Sinks
              </Button>
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.Routes ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.Routes ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.Routes)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                ★ Routes
              </Button>
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.RecentlyUsed ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.RecentlyUsed ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.RecentlyUsed)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                Recently Used
              </Button>
              <Button
                style={{
                  backgroundColor: buttonBgInactive,
                  color: buttonTextInactive
                }}
                onClick={openFullInterface}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                Full View
              </Button>
            </ButtonGroup>
          </HStack>
          
          {/* Second Row - All other buttons including Add */}
          <HStack width="100%" justify="center">
            <ButtonGroup variant="outline" isAttached spacing={0} size="sm">
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.AllSources ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.AllSources ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.AllSources)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                All Sources
              </Button>
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.AllSinks ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.AllSinks ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.AllSinks)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                All Sinks
              </Button>
              <Button
                style={{
                  backgroundColor: currentMenu === MenuLevel.AllRoutes ? buttonBgActive : buttonBgInactive,
                  color: currentMenu === MenuLevel.AllRoutes ? buttonTextActive : buttonTextInactive
                }}
                onClick={() => setCurrentMenu(MenuLevel.AllRoutes)}
                _hover={{ opacity: 0.8 }}
                size="xs"
              >
                All Routes
              </Button>
              
              {/* Add Menu Dropdown as part of the ButtonGroup */}
              <AddMenuDropdown
                buttonBgInactive={buttonBgInactive}
                buttonTextInactive={buttonTextInactive}
                onAddSource={handleAddSource}
                onAddSourceGroup={handleAddSourceGroup}
                onAddSink={handleAddSink}
                onAddSinkGroup={handleAddSinkGroup}
                onAddRoute={handleAddRoute}
              />
            </ButtonGroup>
          </HStack>
        </Flex>
      </Box>
    </Flex>
  );
};

export default DesktopMenu;

/**
 * React component for the main layout of the application.
 * This component provides a consistent structure and navigation across different pages.
 * It includes a header with navigation links, a status section for Primary Sources and now playing information,
 * a main content area for nested routes, and a footer with copyright information.
 * Uses Chakra UI components for consistent styling.
 *
 * @param {React.FC} props - The properties for the component.
 */
import React, { useState, useEffect } from 'react';
import { Link as RouterLink, Outlet } from 'react-router-dom';

type ColorMode = 'light' | 'dark' | 'system';
import {
  Box,
  Flex,
  Heading,
  Text,
  Button,
  Link,
  HStack,
  Container,
  useColorModeValue,
  Modal,
  ModalOverlay,
  ModalContent,
  ModalCloseButton
} from '@chakra-ui/react';
import { ExternalLinkIcon, MoonIcon, SunIcon } from '@chakra-ui/icons';
import { useAppContext } from '../context/AppContext';
import Equalizer from './pages/EqualizerPage';

/**
 * React functional component for the Layout.
 *
 * @returns {JSX.Element} The rendered JSX element.
 */
const Layout: React.FC = () => {
  /**
   * State to keep track of the current color mode (light, dark, or system default).
   */
  const [colorMode, setColorMode] = useState<ColorMode>(() => {
    const savedMode = localStorage.getItem('colorMode');
    return (savedMode as ColorMode) || 'system';
  });

  /**
   * Context values from AppContext.
   */
  const { 
    activeSource, 
    listeningToSink,
    showEqualizerModal,
    selectedEqualizerItem,
    selectedEqualizerType,
    closeEqualizerModal,
    fetchSources,
    fetchSinks,
    fetchRoutes
  } = useAppContext();

  /**
   * Cycles through color modes (light, dark, system default).
   */
  const toggleColorMode = () => {
    const modes: ColorMode[] = ['light', 'dark', 'system'];
    const currentIndex = modes.indexOf(colorMode);
    const newMode = modes[(currentIndex + 1) % modes.length];
    setColorMode(newMode);
    localStorage.setItem('colorMode', newMode);
  };

  /**
   * Applies the color mode to the document body.
   *
   * @param {ColorMode} mode - The color mode to apply.
   */
  const applyColorMode = (mode: ColorMode) => {
    if (mode === 'system') {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      document.body.classList.toggle('dark-mode', prefersDark);
    } else {
      document.body.classList.toggle('dark-mode', mode === 'dark');
    }
  };

  /**
   * Effect to apply the color mode when it changes and listens for system color scheme changes.
   */
  useEffect(() => {
    applyColorMode(colorMode);

    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handleChange = () => {
      if (colorMode === 'system') {
        applyColorMode('system');
      }
    };

    mediaQuery.addListener(handleChange);
    return () => mediaQuery.removeListener(handleChange);
  }, [colorMode]);

  /**
   * Returns the text representation of the current color mode.
   *
   * @returns {string} The color mode text.
   */
  const getCurrentColorModeText = () => {
    switch (colorMode) {
      case 'light':
        return 'Light Mode';
      case 'dark':
        return 'Dark Mode';
      case 'system':
        return 'System Default';
    }
  };

  /**
   * Determines if the current color mode is dark.
   *
   * @returns {boolean} True if dark mode, false otherwise.
   */
  const isDarkMode = colorMode === 'dark' || (colorMode === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);

  /**
   * Fetches data for sources, sinks, and routes when the equalizer modal is closed.
   */
  const handleDataChange = async () => {
    await fetchSources();
    await fetchSinks();
    await fetchRoutes();
  };

  // Color values for light/dark mode
  const bg = useColorModeValue('white', 'gray.800');
  const headerBg = useColorModeValue('blue.500', 'blue.700');
  const textColor = useColorModeValue('gray.800', 'white');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  /**
   * Renders the Layout component.
   *
   * @returns {JSX.Element} The rendered JSX element.
   */
  return (
    <Box bg={bg} color={textColor} minH="100vh" display="flex" flexDirection="column">
      <Flex
        as="header"
        bg={headerBg}
        color="white"
        p={4}
        alignItems="center"
        justifyContent="space-between"
        flexWrap={["wrap", "wrap", "nowrap"]}
        shadow="md"
      >
        <Heading as="h1" size="lg" mr={5}>ScreamRouter</Heading>
        
        <Button
          variant="outline"
          colorScheme="whiteAlpha"
          onClick={toggleColorMode}
          size="sm"
          mr={[0, 0, 5]}
          leftIcon={isDarkMode ? <SunIcon /> : <MoonIcon />}
        >
          {getCurrentColorModeText()}
        </Button>
        
        <HStack
          as="nav"
          spacing={4}
          display={{ base: 'none', md: 'flex' }}
          width={{ base: 'full', md: 'auto' }}
          alignItems="center"
          flexGrow={1}
          mt={{ base: 4, md: 0 }}
        >
          <Link as={RouterLink} to="/" px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            Dashboard
          </Link>
          <Link as={RouterLink} to="/sources" px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            Sources
          </Link>
          <Link as={RouterLink} to="/sinks" px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            Sinks
          </Link>
          <Link as={RouterLink} to="/routes" px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            Routes
          </Link>
          <Link as={RouterLink} to="/desktopmenu" px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            Desktop Menu
          </Link>
          <Link href="https://github.com/netham45/screamrouter/tree/master/Readme" isExternal px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            Docs <ExternalLinkIcon mx="2px" />
          </Link>
          <Link href="https://github.com/netham45/screamrouter" isExternal px={2} py={1} rounded="md" _hover={{ textDecoration: 'none', bg: 'blue.600' }}>
            GitHub <ExternalLinkIcon mx="2px" />
          </Link>
        </HStack>
      </Flex>
      {(activeSource || listeningToSink) && (
        <Box
          p={4}
          bg={useColorModeValue('blue.50', 'blue.900')}
          borderBottomWidth="1px"
          borderColor={borderColor}
        >
        </Box>
      )}
      
      <Box as="main" flex="1" p={6}>
        <Container maxW="container.xl">
          <Outlet />
        </Container>
      </Box>
      
      <Box
        as="footer"
        p={4}
        bg={headerBg}
        color="white"
        textAlign="center"
      >
        <Text>&copy; {new Date().getFullYear()} Netham45</Text>
      </Box>
      
      {showEqualizerModal && selectedEqualizerItem && selectedEqualizerType && (
        <Modal isOpen={true} onClose={closeEqualizerModal} size="xl">
          <ModalOverlay />
          <ModalContent>
            <ModalCloseButton />
            <Equalizer
              item={selectedEqualizerItem}
              type={selectedEqualizerType}
              onClose={closeEqualizerModal}
              onDataChange={handleDataChange}
            />
          </ModalContent>
        </Modal>
      )}
    </Box>
  );
};

export default Layout;

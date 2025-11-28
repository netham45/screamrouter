import React, { useEffect, useState } from 'react';
import {
  Box,
  Flex,
  VStack,
  Heading,
  Divider,
  useColorModeValue,
  CloseButton,
  Text,
} from '@chakra-ui/react';
import NavItem from '../navigation/NavItem';
import { SidebarProps } from '../types';
import ApiService, { type SystemInfo } from '../../../api/api';

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
  systemCaptureDevices,
  systemPlaybackDevices,
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
  const cardBg = useColorModeValue('blue.50', 'whiteAlpha.100');
  const cardBorder = useColorModeValue('blue.100', 'whiteAlpha.300');
  const cardText = useColorModeValue('gray.700', 'whiteAlpha.900');
  const cardMutedText = useColorModeValue('gray.500', 'whiteAlpha.700');
  const totalSystemDevices = systemCaptureDevices.length + systemPlaybackDevices.length;

  const [systemInfo, setSystemInfo] = useState<SystemInfo | null>(null);
  const [systemInfoError, setSystemInfoError] = useState(false);

  useEffect(() => {
    let isMounted = true;

    const fetchSystemInfo = async () => {
      try {
        const response = await ApiService.getSystemInfo();
        if (isMounted) {
          setSystemInfo(response.data);
          setSystemInfoError(false);
        }
      } catch (error) {
        console.error('Failed to fetch system info:', error);
        if (isMounted) {
          setSystemInfoError(true);
        }
      }
    };

    fetchSystemInfo();
    const intervalId = window.setInterval(fetchSystemInfo, 10000);

    return () => {
      isMounted = false;
      window.clearInterval(intervalId);
    };
  }, []);

  const formatPercent = (value: number | null | undefined, digits = 0) => {
    if (value === null || value === undefined || Number.isNaN(value)) {
      return 'n/a';
    }
    return `${value.toFixed(digits)}%`;
  };

  const formatMemoryValue = (value: number | null | undefined) => {
    if (value === null || value === undefined || Number.isNaN(value)) {
      return 'n/a';
    }
    if (value >= 1024) {
      return `${(value / 1024).toFixed(1)} GB`;
    }
    return `${Math.round(value)} MB`;
  };

  const formatLoadAverage = (load: SystemInfo['load_average']) => {
    if (!load) {
      return 'n/a';
    }
    return `${load.one.toFixed(2)} / ${load.five.toFixed(2)} / ${load.fifteen.toFixed(2)}`;
  };

  const formattedLocalTime = systemInfo
    ? new Date(systemInfo.server_time.local_iso).toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      })
    : 'n/a';

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
        <Box
          p={4}
          borderBottomWidth="1px"
          borderColor={borderColor}
        >
          <Flex justify="space-between" align="center" mb={3}>
            <CloseButton
              display={{ base: 'block', md: 'none' }}
              onClick={toggleSidebar}
            />
          </Flex>
          <Box
            borderRadius="lg"
            borderWidth="1px"
            borderColor={cardBorder}
            bg={cardBg}
            p={3}
          >
            {systemInfo ? (
              <>
                <Text fontSize="sm" fontWeight="semibold" color={cardText} noOfLines={1}>
                  {systemInfo.hostname}
                </Text>
                <Text fontSize="xs" color={cardMutedText} mt={1}>
                  {formattedLocalTime} <br />
                  Uptime {systemInfo.uptime_human ?? 'n/a'}
                </Text>
                <Text fontSize="xs" color={cardText} mt={2}>
                  Load {formatLoadAverage(systemInfo.load_average)}
                </Text>
                <Text fontSize="xs" color={cardText}>
                  Mem {formatPercent(systemInfo.memory.used_percent, 0)} • {formatMemoryValue(systemInfo.memory.used_mb)} / {formatMemoryValue(systemInfo.memory.total_mb)}
                </Text>
                <Text fontSize="xs" color={cardText}>
                  PID {systemInfo.process.pid} • CPU {formatPercent(systemInfo.process.cpu_percent, 1)} • RSS {formatMemoryValue(systemInfo.process.memory_rss_mb)}
                </Text>
              </>
            ) : (
              <Text fontSize="xs" color={cardMutedText}>
                {systemInfoError ? 'Unable to load system stats.' : 'Loading system stats...'}
              </Text>
            )}
            {systemInfoError && systemInfo && (
              <Text fontSize="xs" color="red.300" mt={2}>
                Refresh failed
              </Text>
            )}
          </Box>
        </Box>
        
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
              <NavItem
                icon="satellite-dish"
                label="Discovered"
                isActive={currentCategory === 'discovery'}
                onClick={() => {
                  setCurrentCategory('discovery');
                  if (window.innerWidth < 768) toggleSidebar();
                }}
              />
              <NavItem
                icon="plug"
                label="System Audio"
                isActive={currentCategory === 'system-devices'}
                badge={totalSystemDevices || undefined}
                onClick={() => {
                  setCurrentCategory('system-devices');
                  if (window.innerWidth < 768) toggleSidebar();
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
                dataTutorialId="favorites-nav-item"
                onClick={() => {
                  setCurrentCategory('favorites');
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
              Tools
            </Heading>
            <VStack spacing={1} align="stretch">
              <NavItem
                icon="chart-bar"
                label="Stats"
                isActive={currentCategory === 'stats'}
                onClick={() => {
                  setCurrentCategory('stats');
                  if (window.innerWidth < 768) toggleSidebar(); // Close sidebar on mobile
                }}
              />
              <NavItem
                icon="file-alt"
                label="Logs"
                isActive={currentCategory === 'logs'}
                onClick={() => {
                  setCurrentCategory('logs');
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

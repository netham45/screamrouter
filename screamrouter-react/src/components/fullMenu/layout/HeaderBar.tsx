import React from 'react';
import {
  Box,
  Flex,
  Text,
  IconButton,
  useColorModeValue,
  useColorMode,
  HStack,
  Tooltip
} from '@chakra-ui/react';
import { RepeatIcon, SettingsIcon, SunIcon, MoonIcon, HamburgerIcon } from '@chakra-ui/icons';
import { FaPlay, FaStepBackward, FaStepForward } from 'react-icons/fa';
import { HeaderBarProps } from '../types';
import SearchBox from '../search/SearchBox';
import VolumeSlider from '../controls/VolumeSlider';

/**
 * HeaderBar component for the FullMenu.
 * This component displays the application logo, search bar, and global controls.
 * Uses Chakra UI components for consistent styling.
 */
const HeaderBar: React.FC<HeaderBarProps> = ({
  isDarkMode,
  sources,
  sinks,
  routes,
  navigate,
  toggleSidebar,
  activeSource,
  controlSource,
  updateVolume
}) => {
  const { toggleColorMode } = useColorMode();
  // Define colors based on color mode
  const bgColor = useColorModeValue('blue.500', 'blue.700');
  const textColor = useColorModeValue('white', 'white');
  const buttonHoverBg = useColorModeValue('blue.600', 'blue.800');
  
  // Find the active source object to get its volume
  const activeSourceObj = activeSource ? sources.find(s => s.name === activeSource) : null;

  return (
    <Flex
      as="header"
      align="center"
      justify="space-between"
      py={3}
      px={5}
      bg={bgColor}
      color={textColor}
      boxShadow="md"
    >
      <Flex align="center">
        <IconButton
          aria-label="Open menu"
          icon={<HamburgerIcon />}
          variant="ghost"
          color="white"
          _hover={{ bg: buttonHoverBg }}
          display={{ base: 'flex', md: 'none' }}
          mr={2}
          onClick={toggleSidebar}
        />
        <Box mr={2} fontSize="xl">
          {/* Replace with actual icon or logo */}
          <i className="fas fa-broadcast-tower"></i>
        </Box>
        <Text fontSize="xl" fontWeight="bold">
          ScreamRouter
        </Text>
      </Flex>
      
      <Box flex="1" mx={6} display="flex" alignItems="center">
      <SearchBox
          sources={sources}
          sinks={sinks}
          routes={routes}
          navigate={navigate}
        />
        {activeSource && (
          <>
            {controlSource && (
              <HStack spacing={1} mr={4}>
                <Tooltip label="Previous Track">
                  <IconButton
                    aria-label="Previous Track"
                    icon={<FaStepBackward />}
                    size="sm"
                    colorScheme="purple"
                    variant="ghost"
                    color="white"
                    _hover={{ bg: buttonHoverBg }}
                    onClick={() => controlSource(activeSource, 'prevtrack')}
                    isDisabled={!activeSource}
                  />
                </Tooltip>
                <Tooltip label="Play/Pause">
                  <IconButton
                    aria-label="Play/Pause"
                    icon={<FaPlay />}
                    size="sm"
                    colorScheme="purple"
                    variant="ghost"
                    color="white"
                    _hover={{ bg: buttonHoverBg }}
                    onClick={() => controlSource(activeSource, 'play')}
                    isDisabled={!activeSource}
                  />
                </Tooltip>
                <Tooltip label="Next Track">
                  <IconButton
                    aria-label="Next Track"
                    icon={<FaStepForward />}
                    size="sm"
                    colorScheme="purple"
                    variant="ghost"
                    color="white"
                    _hover={{ bg: buttonHoverBg }}
                    onClick={() => controlSource(activeSource, 'nexttrack')}
                    isDisabled={!activeSource}
                  />
                </Tooltip>
              </HStack>
            )}
            {updateVolume && activeSourceObj && (
              <Box mr={4} width="150px">
                <VolumeSlider
                  showLabel={false}
                  value={activeSourceObj.volume || 0}
                  onChange={(volume) => {
                    if (updateVolume) {
                      updateVolume('sources', activeSource, volume);
                    }
                  }}
                />
              </Box>
            )}
          </>
        )}
      </Box>
      
      <HStack spacing={2}>
        <IconButton
          aria-label="Refresh"
          icon={<RepeatIcon />}
          variant="ghost"
          color="white"
          _hover={{ bg: buttonHoverBg }}
        />
        <IconButton
          aria-label="Settings"
          icon={<SettingsIcon />}
          variant="ghost"
          color="white"
          _hover={{ bg: buttonHoverBg }}
        />
        <IconButton
          aria-label="Toggle color mode"
          icon={isDarkMode ? <SunIcon /> : <MoonIcon />}
          variant="ghost"
          color="white"
          _hover={{ bg: buttonHoverBg }}
          onClick={toggleColorMode}
        />
      </HStack>
    </Flex>
  );
};

export default HeaderBar;
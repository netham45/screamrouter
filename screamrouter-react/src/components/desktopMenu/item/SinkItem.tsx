/**
 * Compact sink item component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React, { useState, useEffect } from 'react';
import { 
  Tr, Td, Text, Box,
  Menu, MenuButton, MenuList, MenuItem, MenuGroup,
  MenuDivider, Slider, SliderTrack,
  SliderFilledTrack, SliderThumb, Icon, HStack
} from '@chakra-ui/react';
import { useColorContext } from '../context/ColorContext';
import ApiService, { Sink, Route } from '../../../api/api';
import { DesktopMenuActions } from '../types';
import { openEditPage } from '../../fullMenu/utils';
import ActionButton from '../controls/ActionButton';
import VolumeControl from '../controls/VolumeControl';
import { addToRecents } from '../../../utils/recents';

interface SinkItemProps {
  /**
   * Sink object
   */
  sink: Sink;
  
  /**
   * Routes associated with this sink
   */
  routes: Route[];
  
  /**
   * Whether the sink is starred
   */
  isStarred: boolean;
  
  /**
   * Whether this sink is currently being listened to.
   */
  isListening: boolean;
  
  /**
   * Name of the sink being visualized (if any)
   */
  visualizingSink: string | null;
  
  /**
   * Actions for the DesktopMenu
   */
  actions: DesktopMenuActions;
  
  /**
   * Whether the item is selected
   */
  isSelected?: boolean;
}

/**
 * Route menu item component
 */
const RouteMenuItem = ({ route, actions }: { route: Route; actions: DesktopMenuActions }) => (
  <MenuItem 
    onClick={() => actions.toggleEnabled('routes', route.name, !route.enabled)}
  >
    <HStack spacing={2}>
      <Icon 
        boxSize={3}
        viewBox="0 0 200 200"
        color={route.enabled ? 'green.500' : 'red.500'}
      >
        <path
          fill="currentColor"
          d={route.enabled ? 
            "M 100, 100 m -75, 0 a 75,75 0 1,0 150,0 a 75,75 0 1,0 -150,0" :  // Circle for enabled
            "M 50,50 L 150,150 M 50,150 L 150,50"  // X for disabled
          }
        />
      </Icon>
      <Text bgColor="transparent !important">{route.name}</Text>
    </HStack>
  </MenuItem>
);

/**
 * A compact sink item component optimized for the DesktopMenu interface.
 */
const SinkItem: React.FC<SinkItemProps> = ({
  sink,
  routes,
  isStarred,
  actions,
  isSelected = false,
  isListening,
  visualizingSink
}) => {
  // State to track if processes exist for this sink's IP
  const [hasProcesses, setHasProcesses] = useState(false);
  
  // Check if processes exist for this sink's IP
  useEffect(() => {
    const checkForProcesses = async () => {
      if (!sink.ip) return;
      
      try {
        const response = await ApiService.getSources();
        const allSources = Object.values(response.data);
        
        // Check if any sources are processes with a tag that starts with this sink's IP
        const processesForIp = allSources.filter(source => 
          source.is_process && source.tag && source.tag.startsWith(sink.ip)
        );
        
        setHasProcesses(processesForIp.length > 0);
      } catch (error) {
        console.error('Error checking for processes:', error);
      }
    };
    
    checkForProcesses();
  }, [sink.ip]);

  // Get colors from context
  const { getLighterColor, getDarkerColor } = useColorContext();
  const selectedBg = getLighterColor(1.25);

  // Handle right click
  const handleContextMenu = (e: React.MouseEvent) => {
    e.preventDefault();
  };

  // Get active routes count
  const activeRoutes = routes.filter(r => r.enabled).length;

  // Check if this is a real sink (has a port)
  const isRealSink = Boolean(sink.port);

  // Split routes into main and overflow
  const mainRoutes = routes.slice(0, 3);
  const overflowRoutes = routes.slice(3);
  const hasOverflow = overflowRoutes.length > 0;
  
  // Function to open process list page
  const openProcessList = () => {
    window.open(`/site/processes/${sink.ip}`, '_blank');
  };
  
  return (
    <Tr 
      bg={isSelected ? selectedBg : undefined}
      id={`sinks-${encodeURIComponent(sink.name)}`}
      onContextMenu={handleContextMenu}
      cursor="context-menu"
      sx={
        {"button":{
           "textColor": "white",
           "backgroundColor": getDarkerColor(1.15),
           ":hover": {
             "textColor": getLighterColor(339),
             "backgroundColor": getDarkerColor(.7)
            }
          },
        ":hover": {
          "textColor": getLighterColor(339),
          "backgroundColor": getDarkerColor(.7)
          },
        "p.chakra-menu__group__title, div p.chakra-text": {
          "textColor": "white",
          "backgroundColor": getDarkerColor(1.15)
        }
      }
    }
    >
      {/* Name */}
      <Td>
        <Menu
          placement="auto"
          strategy="fixed"
          boundary="scrollParent"
          flip
          preventOverflow
        >
          <MenuButton as={Text} fontWeight="normal">
            {sink.name}
          </MenuButton>
          <MenuList maxH="calc(100vh - 100px)" overflowY="auto" overflowX="hidden"  pr={2.5} textColor={getDarkerColor(.01)} backgroundColor={getLighterColor(1.15)}>
            <Box px={3} py={2}>
              <Text fontSize="sm">
                {activeRoutes} active route{activeRoutes !== 1 ? 's' : ''}
                {isRealSink && ' • Real Device'}
                {sink.timeshift !== 0 && ` • Timeshift: ${sink.timeshift}s`}
              </Text>
            </Box>

            <MenuDivider />
            <MenuItem 
              onClick={() => actions.toggleStar('sinks', sink.name)}
              role="menuitem"
              aria-label={isStarred ? 'Remove from Favorites' : 'Add to Favorites'}
            >
              {isStarred ? '★ Remove from Favorites' : '☆ Add to Favorites'}
            </MenuItem>
            <MenuItem onClick={() => {
              actions.toggleEnabled('sinks', sink.name, !sink.enabled);
              addToRecents('sinks', sink.name);
            }}>
              {sink.enabled ? 'Disable' : 'Enable'}
            </MenuItem>
            <MenuItem onClick={() => openEditPage('sinks', sink)}>
              Edit Settings
            </MenuItem>
            <MenuItem onClick={() => actions.showEqualizer(true, 'sinks', sink)}>
              Equalize
            </MenuItem>
            {/* --- New MenuItem for Speaker Layout --- */}
            <MenuItem onClick={() => actions.showSpeakerLayoutPage('sinks', sink)}>
              Speaker Layout
            </MenuItem>
            {/* --- End New MenuItem --- */}
            <MenuItem onClick={() => actions.confirmDelete('sinks', sink.name)}>
              Delete
            </MenuItem>

            {isRealSink && (
              <>
                <MenuDivider />
                <MenuItem 
                  onClick={() => actions.transcribeSink(sink.ip)}
                >
                  Transcribe
                </MenuItem>
                <MenuItem 
                  onClick={() => actions.visualizeSink(visualizingSink === sink.name ? null : sink.name)}
                  _active={{ bg: visualizingSink === sink.name ? 'blue.500' : undefined }}
                >
                  Visualize
                </MenuItem>
                <MenuItem
                  onClick={() => window.open(`/site/listen/sink/${encodeURIComponent(sink.name)}`, '_blank')}
                  _active={{ bg: isListening ? 'blue.500' : undefined }}
                >
                  🎧 {isListening ? 'Stop Listening' : 'Listen'}
                </MenuItem>
                {hasProcesses && (
                  <MenuItem onClick={openProcessList}>
                    View Processes
                  </MenuItem>
                )}
              </>
            )}

            <MenuDivider />
            <MenuGroup title="Routes">
              {mainRoutes.map(route => (
                <RouteMenuItem key={route.name} route={route} actions={actions} />
              ))}
              {hasOverflow && (
                <Menu placement="right-start">
                  <MenuItem as={MenuButton}>
                    <HStack justify="space-between" width="100%">
                      <Text>More Routes...</Text>
                      <Icon viewBox="0 0 24 24" boxSize={5}>
                        <path fill="currentColor" d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/>
                      </Icon>
                    </HStack>
                  </MenuItem>
                  <MenuList>
                    {overflowRoutes.map(route => (
                      <RouteMenuItem key={route.name} route={route} actions={actions} />
                    ))}
                  </MenuList>
                </Menu>
              )}
            </MenuGroup>

            <MenuDivider />
            <Box px={3} py={2}>
              <Text mb={2} fontSize="sm">Timeshift</Text>
              <Slider
                min={-60}
                max={0}
                step={1}
                value={-sink.timeshift || 0}
                maxW="95%"
                onChange={(val) => actions.updateTimeshift('sinks', sink.name, -val)}
              >
                <SliderTrack>
                  <SliderFilledTrack />
                </SliderTrack>
                <SliderThumb />
              </Slider>
            </Box>
          </MenuList>
        </Menu>
      </Td>
      
      {/* Status */}
      <Td>
        <ActionButton
          icon={sink.enabled ? 'volume' : 'x'}
          isActive={sink.enabled}
          onClick={() => {
            actions.toggleEnabled('sinks', sink.name, !sink.enabled);
            addToRecents('sinks', sink.name);
          }}
        />
      </Td>
      
      {/* Volume */}
      <Td>
        <Box position="relative" minW="250px">
          <VolumeControl
            maxWidth="250px"
            value={sink.volume}
            onChange={(value) => actions.updateVolume('sinks', sink.name, value)}
            enabled={sink.enabled}
          />
        </Box>
      </Td>
    </Tr>
  );
};

export default SinkItem;

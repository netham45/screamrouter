/**
 * Compact source item component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React, { useState, useEffect } from 'react';
import { 
  Tr, Td, Text, HStack, Box,
  Menu, MenuButton, MenuList, MenuItem, MenuGroup,
  MenuDivider, Slider, SliderTrack,
  SliderFilledTrack, SliderThumb, Icon
} from '@chakra-ui/react';
import { useColorContext } from '../context/ColorContext';
import ApiService, { Source, Route } from '../../../api/api';
import { DesktopMenuActions } from '../types';
import { openEditPage } from '../../fullMenu/utils';
import ActionButton from '../controls/ActionButton';
import VolumeControl from '../controls/VolumeControl';
import { addToRecents } from '../../../utils/recents';

interface SourceItemProps {
  /**
   * Source object
   */
  source: Source;
  
  /**
   * Routes associated with this source
   */
  routes: Route[];
  
  /**
   * Whether the source is starred
   */
  isStarred: boolean;
  
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
 * A compact source item component optimized for the DesktopMenu interface.
 */
const SourceItem: React.FC<SourceItemProps> = ({
  source,
  routes,
  isStarred,
  actions,
  isSelected = false
}) => {
  // State to track if processes exist for this source's IP
  const [hasProcesses, setHasProcesses] = useState(false);
  
  // Check if processes exist for this source's IP
  useEffect(() => {
    const checkForProcesses = async () => {
      if (!source.ip) return;
      
      try {
        const response = await ApiService.getSources();
        const allSources = Object.values(response.data);
        
        // Check if any sources are processes with a tag that starts with this source's IP
        const processesForIp = allSources.filter(src => 
          src.is_process && src.tag && src.tag.startsWith(source.ip)
        );
        
        setHasProcesses(processesForIp.length > 0);
      } catch (error) {
        console.error('Error checking for processes:', error);
      }
    };
    
    checkForProcesses();
  }, [source.ip]);

  // Get colors from context
  const { getLighterColor, getDarkerColor } = useColorContext();
  const selectedBg = getLighterColor(1.25);

  // Handle right click
  const handleContextMenu = (e: React.MouseEvent) => {
    e.preventDefault();
  };
  
  // Get active routes count
  const activeRoutes = routes.filter(r => r.enabled).length;

  // Split routes into main and overflow
  const mainRoutes = routes.slice(0, 3);
  const overflowRoutes = routes.slice(3);
  const hasOverflow = overflowRoutes.length > 0;
  
  // Function to open process list page
  const openProcessList = () => {
    window.open(`/site/processes/${source.ip}`, '_blank');
  };
  
  return (
    <Tr 
      bg={isSelected ? selectedBg : undefined}
      id={`sources-${encodeURIComponent(source.name)}`}
      onContextMenu={handleContextMenu}
      cursor="context-menu"
      sx={
        {"button":{
           "textColor": "white",
           "backgroundColor": getDarkerColor(1.15),
           ":nth-child(1)" : {
            "backgroundColor": getDarkerColor(1),
           },
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
            {source.name}
          </MenuButton>
          <MenuList maxH="calc(100vh - 100px)" overflowY="auto" overflowX="hidden"  pr={2.5} textColor={getDarkerColor(.01)} backgroundColor={getLighterColor(1.15)}>
            <Box px={3} py={2}>
              <Text fontSize="sm">
                {activeRoutes} active route{activeRoutes !== 1 ? 's' : ''}
                {source.vnc_ip && ` • VNC: ${source.vnc_ip}`}
                {source.timeshift !== 0 && ` • Timeshift: ${source.timeshift}s`}
              </Text>
            </Box>

            <MenuDivider />
            <MenuItem 
              onClick={() => actions.toggleStar('sources', source.name)}
              role="menuitem"
              aria-label={isStarred ? 'Remove from Favorites' : 'Add to Favorites'}
            >
              {isStarred ? '★ Remove from Favorites' : '☆ Add to Favorites'}
            </MenuItem>
            <MenuItem 
              onClick={() => actions.toggleActiveSource(source.name)}
              role="menuitem"
              aria-label={source.is_primary ? 'Remove as Primary Source' : 'Set as Primary Source'}
            >
              {source.is_primary ? 'Remove as Primary Source' : 'Set as Primary Source'}
            </MenuItem>
            <MenuItem onClick={() => actions.toggleEnabled('sources', source.name, !source.enabled)}>
              {source.enabled ? 'Disable' : 'Enable'}
            </MenuItem>
            <MenuItem onClick={() => openEditPage('sources', source)}>
              Edit Settings
            </MenuItem>
            <MenuItem onClick={() => actions.showEqualizer(true, 'sources', source)}>
              Equalize
            </MenuItem>
            {/* --- New MenuItem for Speaker Layout --- */}
            <MenuItem onClick={() => actions.showSpeakerLayoutPage('sources', source)}>
              Speaker Layout
            </MenuItem>
            {/* --- End New MenuItem --- */}
            <MenuItem onClick={() => window.open(`/site/listen/source/${encodeURIComponent(source.name)}`, '_blank')}>
              🎧 Listen
            </MenuItem>
            <MenuItem onClick={() => actions.confirmDelete('sources', source.name)}>
              Delete
            </MenuItem>

            {(source.vnc_ip || hasProcesses) && (
              <>
                <MenuDivider />
                {source.vnc_ip && (
                  <>
                    <MenuItem onClick={() => actions.showVNC(true, source)}>
                      VNC
                    </MenuItem>
                    <MenuItem onClick={() => actions.controlSource(source.name, 'prevtrack')}>
                      Previous Track
                    </MenuItem>
                    <MenuItem onClick={() => {
                      actions.controlSource(source.name, 'play');
                      addToRecents('sources', source.name);
                    }}>
                      Play/Pause
                    </MenuItem>
                    <MenuItem onClick={() => actions.controlSource(source.name, 'nexttrack')}>
                      Next Track
                    </MenuItem>
                  </>
                )}
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
                value={-source.timeshift || 0}
                maxW="95%"
                onChange={(val) => actions.updateTimeshift('sources', source.name, -val)}
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
      
      {/* Media Controls (only if VNC IP is set) */}
      <Td>
        {source.vnc_ip && (
          <HStack spacing={1}>
            <ActionButton
              icon="desktop"
              onClick={() => actions.showVNC(true, source)}
            />
            <ActionButton
              icon="prevtrack"
              onClick={() => actions.controlSource(source.name, 'prevtrack')}
            />
            <ActionButton
              icon="play"
              onClick={() => {
                actions.controlSource(source.name, 'play');
                addToRecents('sources', source.name);
              }}
            />
            <ActionButton
              icon="nexttrack"
              onClick={() => actions.controlSource(source.name, 'nexttrack')}
            />
          </HStack>
        )}
      </Td>
      
      {/* Status */}
      <Td>
        <ActionButton
          icon={source.enabled ? 'volume' : 'x'}
          isActive={source.enabled}
          onClick={() => actions.toggleEnabled('sources', source.name, !source.enabled)}
        />
      </Td>
      
      {/* Volume */}
      <Td>
        <Box position="relative" minW="250px">
          <VolumeControl
            maxWidth="250px"
            value={source.volume}
            onChange={(value) => actions.updateVolume('sources', source.name, value)}
            enabled={source.enabled}
          />
        </Box>
      </Td>
    </Tr>
  );
};

export default SourceItem;

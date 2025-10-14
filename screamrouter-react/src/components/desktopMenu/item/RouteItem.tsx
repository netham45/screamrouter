/**
 * Compact route item component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { 
  Tr, Td, Text, Box,
  Menu, MenuButton, MenuList, MenuItem, MenuGroup,
  MenuDivider, Slider, SliderTrack,
  SliderFilledTrack, SliderThumb, Icon, HStack
} from '@chakra-ui/react';
import { useColorContext } from '../context/ColorContext';
import { Route } from '../../../api/api';
import { DesktopMenuActions } from '../types';
import { openEditPage } from '../../fullMenu/utils';
import ActionButton from '../controls/ActionButton';
import VolumeControl from '../controls/VolumeControl';
import { addToRecents } from '../../../utils/recents';

interface RouteItemProps {
  /**
   * Route object
   */
  route: Route;
  
  /**
   * Whether the route is starred
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
 * A compact route item component optimized for the DesktopMenu interface.
 */
const RouteItem: React.FC<RouteItemProps> = ({
  route,
  isStarred,
  actions,
  isSelected = false
}) => {
  // Get colors from context
  const { getLighterColor, getDarkerColor } = useColorContext();
  const selectedBg = getLighterColor(1.25);

  // Handle right click
  const handleContextMenu = (e: React.MouseEvent) => {
    e.preventDefault();
  };
  
  return (
    <Tr 
      bg={isSelected ? selectedBg : undefined}
      id={`routes-${encodeURIComponent(route.name)}`}
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
            {route.name}
          </MenuButton>
          <MenuList maxH="calc(100vh - 100px)" overflowY="auto" overflowX="hidden" pr={2.5} textColor={getDarkerColor(.01)} backgroundColor={getLighterColor(1.15)}>
            <Box px={3} py={2}>
              <Text fontSize="sm">
                {route.source} → {route.sink}
                {route.timeshift !== 0 && ` • Timeshift: ${route.timeshift}s`}
              </Text>
            </Box>

            <MenuDivider />
            <MenuItem
              onClick={() => actions.toggleStar('routes', route.name)}
              role="menuitem"
              aria-label={isStarred ? 'Remove from Favorites' : 'Add to Favorites'}
            >
              {isStarred ? '★ Remove from Favorites' : '☆ Add to Favorites'}
            </MenuItem>
            <MenuItem onClick={() => {
              actions.toggleEnabled('routes', route.name, !route.enabled);
              addToRecents('routes', route.name);
            }}>
              {route.enabled ? 'Disable' : 'Enable'}
            </MenuItem>
            <MenuItem onClick={() => openEditPage('routes', route)}>
              Edit Settings
            </MenuItem>
            <MenuItem onClick={() => actions.showEqualizer(true, 'routes', route)}>
              Equalize
            </MenuItem>
            {/* --- New MenuItem for Speaker Layout --- */}
            <MenuItem onClick={() => actions.showSpeakerLayoutPage('routes', route)}>
              Speaker Layout
            </MenuItem>
            {/* --- End New MenuItem --- */}
            <MenuItem onClick={() => window.open(`/site/listen/route/${encodeURIComponent(route.name)}`, '_blank')}>
              🎧 Listen
            </MenuItem>
            <MenuItem onClick={() => actions.confirmDelete('routes', route.name)}>
              Delete
            </MenuItem>

            <MenuDivider />
            <MenuGroup title="Source">
              <MenuItem 
                onClick={() => actions.toggleEnabled('sources', route.source, !route.enabled)}
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
                  <Text bgColor="transparent !important">{route.source}</Text>
                </HStack>
              </MenuItem>
            </MenuGroup>

            <MenuGroup title="Sink">
              <MenuItem 
                onClick={() => actions.toggleEnabled('sinks', route.sink, !route.enabled)}
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
                  <Text bgColor="transparent !important">{route.sink}</Text>
                </HStack>
              </MenuItem>
            </MenuGroup>

            <MenuDivider />
            <Box px={3} py={2}>
              <Text mb={2} fontSize="sm">Timeshift</Text>
              <Slider
                min={-60}
                max={0}
                step={1}
                value={-route.timeshift || 0}
                maxW="95%"
                onChange={(val) => actions.updateTimeshift('routes', route.name, -val)}
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
          icon={route.enabled ? 'volume' : 'x'}
          isActive={route.enabled}
          onClick={() => {
            actions.toggleEnabled('routes', route.name, !route.enabled);
            addToRecents('routes', route.name);
          }}
        />
      </Td>
      
      {/* Volume */}
      <Td>
        <Box position="relative" minW="250px">
          <VolumeControl
            maxWidth="250px"
            value={route.volume}
            onChange={(value) => actions.updateVolume('routes', route.name, value)}
            enabled={route.enabled}
          />
        </Box>
      </Td>
    </Tr>
  );
};

export default RouteItem;

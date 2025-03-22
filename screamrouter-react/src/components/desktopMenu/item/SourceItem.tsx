/**
 * Compact source item component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { Tr, Td, Text, HStack, useColorModeValue, Box } from '@chakra-ui/react';
import { Source, Route } from '../../../api/api';
import { DesktopMenuActions } from '../types';
import ActionButton from '../controls/ActionButton';
import VolumeControl from '../controls/VolumeControl';

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
   * Actions for the DesktopMenu
   */
  actions: DesktopMenuActions;
  
  /**
   * Whether the item is selected
   */
  isSelected?: boolean;
}

/**
 * A compact source item component optimized for the DesktopMenu interface.
 */
const SourceItem: React.FC<SourceItemProps> = ({
  source,
  actions,
  isSelected = false
}) => {
  // Color values for light/dark mode
  const selectedBg = useColorModeValue('blue.50', 'blue.900');
  
  return (
    <Tr 
      bg={isSelected ? selectedBg : undefined}
      id={`sources-${encodeURIComponent(source.name)}`}
    >
      {/* Name */}
      <Td>
        <Text fontWeight="medium">{source.name}</Text>
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
              onClick={() => actions.controlSource(source.name, 'play')}
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
          icon={source.enabled ? 'check' : 'x'}
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
          />
        </Box>
      </Td>
    </Tr>
  );
};

export default SourceItem;
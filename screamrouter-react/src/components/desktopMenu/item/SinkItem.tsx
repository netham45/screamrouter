/**
 * Compact sink item component for DesktopMenu.
 * Optimized for display in the slide-out panel.
 */
import React from 'react';
import { Tr, Td, Text, useColorModeValue, Box } from '@chakra-ui/react';
import { Sink, Route } from '../../../api/api';
import { DesktopMenuActions } from '../types';
import ActionButton from '../controls/ActionButton';
import VolumeControl from '../controls/VolumeControl';

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
   * Name of the sink being listened to (if any)
   */
  listeningToSink: string | null;
  
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
 * A compact sink item component optimized for the DesktopMenu interface.
 */
const SinkItem: React.FC<SinkItemProps> = ({
  sink,
  actions,
  isSelected = false
}) => {
  // Color values for light/dark mode
  const selectedBg = useColorModeValue('blue.50', 'blue.900');
  
  return (
    <Tr 
      bg={isSelected ? selectedBg : undefined}
      id={`sinks-${encodeURIComponent(sink.name)}`}
    >
      {/* Name */}
      <Td>
        <Text fontWeight="medium">{sink.name}</Text>
      </Td>
      
      {/* Status */}
      <Td>
        <ActionButton
          icon={sink.enabled ? 'check' : 'x'}
          isActive={sink.enabled}
          onClick={() => actions.toggleEnabled('sinks', sink.name, !sink.enabled)}
        />
      </Td>
      
      {/* Volume */}
      <Td>
        <Box position="relative" minW="250px">
          <VolumeControl
            maxWidth="250px"
            value={sink.volume}
            onChange={(value) => actions.updateVolume('sinks', sink.name, value)}
          />
        </Box>
      </Td>
    </Tr>
  );
};

export default SinkItem;
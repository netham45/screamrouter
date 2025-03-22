/**
 * Simplified action button component for DesktopMenu.
 * Optimized for compact display in the slide-out panel.
 */
import React from 'react';
import { IconButton } from '@chakra-ui/react';
import { 
  FaStar, FaCheck, FaTimes, FaVolumeUp, FaCogs, 
  FaDesktop, FaPlay, FaHeadphones, FaWaveSquare, 
  FaArrowRight, FaArrowLeft, FaPause, FaStepBackward, 
  FaStepForward
} from 'react-icons/fa';

interface ActionButtonProps {
  /**
   * Icon name to display
   */
  icon: string;
  
  /**
   * Function to call when button is clicked
   */
  onClick: () => void;
  
  /**
   * Whether the button is in active state
   */
  isActive?: boolean;
  
  /**
   * Additional props to pass to the IconButton
   */
  [key: string]: unknown;
}

/**
 * A compact action button component optimized for the DesktopMenu interface.
 * Uses icons instead of text to save space.
 */
const ActionButton: React.FC<ActionButtonProps> = ({
  icon,
  onClick,
  isActive = false,
  ...rest
}) => {
  // Map icon string to icon component
  const getIconComponent = (iconName: string) => {
    switch (iconName) {
      case 'star': return <FaStar />;
      case 'check': return <FaCheck />;
      case 'x': return <FaTimes />;
      case 'volume': return <FaVolumeUp />;
      case 'equalizer': return <FaCogs />;
      case 'desktop': return <FaDesktop />;
      case 'play': return <FaPlay />;
      case 'pause': return <FaPause />;
      case 'prevtrack': return <FaStepBackward />;
      case 'nexttrack': return <FaStepForward />;
      case 'headphones': return <FaHeadphones />;
      case 'waveform': return <FaWaveSquare />;
      case 'arrow-right': return <FaArrowRight />;
      case 'arrow-left': return <FaArrowLeft />;
      default: return <FaCheck />; // Fallback icon
    }
  };

  return (
    <IconButton
      aria-label={icon}
      icon={getIconComponent(icon)}
      size="xs"
      colorScheme={isActive ? 'blue' : 'gray'}
      onClick={onClick}
      _hover={{ opacity: 0.8 }}
      {...rest}
    />
  );
};

export default ActionButton;
/**
 * React component for rendering an enable/disable button.
 * Uses Chakra UI Button component with different color schemes based on enabled state.
 */
import React from 'react';
import { Button } from '@chakra-ui/react';

/**
 * Interface defining the props for the EnableButton component.
 */
interface EnableButtonProps {
  /**
   * Boolean indicating whether the button is currently enabled.
   */
  isEnabled: boolean;
  /**
   * Callback function to be executed when the button is clicked.
   */
  onClick: () => void;
}

/**
 * React functional component for rendering an enable/disable button using Chakra UI.
 *
 * @param {EnableButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const EnableButton: React.FC<EnableButtonProps> = ({ isEnabled, onClick }) => (
  <Button
    onClick={onClick}
    colorScheme={isEnabled ? 'green' : 'red'}
    size="sm"
    _hover={{ transform: 'translateY(-2px)' }}
    transition="all 0.3s ease"
  >
    {isEnabled ? 'Enabled' : 'Disabled'}
  </Button>
);

export default EnableButton;

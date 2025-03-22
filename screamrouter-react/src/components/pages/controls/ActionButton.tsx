/**
 * React component for rendering an action button.
 * Uses Chakra UI Button component for consistent styling.
 */
import React from 'react';
import { Button, ButtonProps } from '@chakra-ui/react';

/**
 * Interface defining the props for the ActionButton component.
 */
interface ActionButtonProps extends Omit<ButtonProps, 'onClick'> {
  /**
   * Callback function to be executed when the button is clicked.
   */
  onClick: () => void;
  /**
   * The content of the button, typically text or an icon.
   */
  children: React.ReactNode;
}

/**
 * React functional component for rendering an action button using Chakra UI.
 *
 * @param {ActionButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ActionButton: React.FC<ActionButtonProps> = ({ onClick, children, ...rest }) => (
  <Button
    onClick={onClick}
    size="sm"
    colorScheme="blue"
    _hover={{ transform: 'translateY(-2px)' }}
    transition="all 0.3s ease"
    {...rest}
  >
    {children}
  </Button>
);

export default ActionButton;

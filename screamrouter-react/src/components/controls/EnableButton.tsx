/**
 * React component for rendering an enable/disable button.
 * It uses a common action button and changes its appearance based on whether it is enabled or disabled.
 */
import React from 'react';
import { ActionButton } from '../../utils/commonUtils';

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
 * React functional component for rendering an enable/disable button.
 *
 * @param {EnableButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const EnableButton: React.FC<EnableButtonProps> = ({ isEnabled, onClick }) => (
  <ActionButton 
    onClick={onClick}
    className={isEnabled ? 'enabled' : 'disabled'}
  >
    {isEnabled ? 'Enabled' : 'Disabled'}
  </ActionButton>
);

export default EnableButton;

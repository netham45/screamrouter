/**
 * React component for rendering an action button.
 * It wraps a common action button and passes props like onClick, className, and children.
 */
import React from 'react';
import { ActionButton as CommonActionButton } from '../../utils/commonUtils';

/**
 * Interface defining the props for the ActionButton component.
 */
interface ActionButtonProps {
  /**
   * Callback function to be executed when the button is clicked.
   */
  onClick: () => void;
  /**
   * Optional CSS class name to apply to the button.
   */
  className?: string;
  /**
   * The content of the button, typically text or an icon.
   */
  children: React.ReactNode;
}

/**
 * React functional component for rendering an action button.
 *
 * @param {ActionButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ActionButton: React.FC<ActionButtonProps> = ({ onClick, className, children }) => (
  <CommonActionButton onClick={onClick} className={className}>
    {children}
  </CommonActionButton>
);

export default ActionButton;

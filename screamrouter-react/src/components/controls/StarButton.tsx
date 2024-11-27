/**
 * React component for rendering a star button that toggles between starred and unstarred states.
 */
import React from 'react';
import { ActionButton } from '../../utils/commonUtils';

/**
 * Interface defining the props for the StarButton component.
 */
interface StarButtonProps {
  /**
   * Boolean indicating whether the item is currently starred.
   */
  isStarred: boolean;
  /**
   * Callback function to handle clicking the star button.
   */
  onClick: () => void;
}

/**
 * React functional component for rendering a star button.
 *
 * @param {StarButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const StarButton: React.FC<StarButtonProps> = ({ isStarred, onClick }) => (
  <ActionButton onClick={onClick}>
    {isStarred ? '★' : '☆'}
  </ActionButton>
);

export default StarButton;

/**
 * React component for rendering a star button that toggles between starred and unstarred states.
 * Uses Chakra UI IconButton for consistent styling.
 */
import React from 'react';
import { IconButton } from '@chakra-ui/react';
import { StarIcon } from '@chakra-ui/icons';

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
 * React functional component for rendering a star button using Chakra UI.
 *
 * @param {StarButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const StarButton: React.FC<StarButtonProps> = ({ isStarred, onClick }) => (
  <IconButton
    aria-label={isStarred ? "Unstar item" : "Star item"}
    icon={<StarIcon />}
    onClick={onClick}
    size="sm"
    colorScheme={isStarred ? "yellow" : "gray"}
    variant={isStarred ? "solid" : "outline"}
    _hover={{ transform: 'translateY(-2px)' }}
    transition="all 0.3s ease"
  />
);

export default StarButton;

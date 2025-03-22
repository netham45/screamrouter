/**
 * React component for toggling between grid and list view modes.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  ButtonGroup,
  Button,
  Icon,
  useColorModeValue
} from '@chakra-ui/react';
import { ViewMode } from '../types';
import { FaThLarge, FaList } from 'react-icons/fa';

/**
 * Interface defining the props for the ViewModeToggle component.
 */
interface ViewModeToggleProps {
  /**
   * The current view mode.
   */
  viewMode: ViewMode;
  /**
   * Callback function to handle changes in the view mode.
   */
  onChange: (mode: ViewMode) => void;
}

/**
 * React functional component for rendering a view mode toggle using Chakra UI.
 *
 * @param {ViewModeToggleProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ViewModeToggle: React.FC<ViewModeToggleProps> = ({ viewMode, onChange }) => {
  // Color values for light/dark mode
  const buttonBg = useColorModeValue('white', 'gray.800');
  const buttonBorder = useColorModeValue('gray.200', 'gray.600');
  const activeBg = useColorModeValue('blue.500', 'blue.400');
  const activeColor = 'white';
  const inactiveColor = useColorModeValue('gray.600', 'gray.400');

  return (
    <ButtonGroup size="sm" isAttached variant="outline">
      <Button
        leftIcon={<Icon as={FaThLarge} />}
        onClick={() => onChange('grid')}
        bg={viewMode === 'grid' ? activeBg : buttonBg}
        color={viewMode === 'grid' ? activeColor : inactiveColor}
        borderColor={buttonBorder}
        _hover={{
          bg: viewMode === 'grid' ? activeBg : useColorModeValue('gray.100', 'gray.700')
        }}
        aria-label="Grid view"
        title="Grid view"
      >
        Grid
      </Button>
      <Button
        leftIcon={<Icon as={FaList} />}
        onClick={() => onChange('list')}
        bg={viewMode === 'list' ? activeBg : buttonBg}
        color={viewMode === 'list' ? activeColor : inactiveColor}
        borderColor={buttonBorder}
        _hover={{
          bg: viewMode === 'list' ? activeBg : useColorModeValue('gray.100', 'gray.700')
        }}
        aria-label="List view"
        title="List view"
      >
        List
      </Button>
    </ButtonGroup>
  );
};

export default ViewModeToggle;
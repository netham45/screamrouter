/**
 * React component for rendering a search input field.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  InputGroup,
  InputLeftElement,
  Input,
  useColorModeValue,
  Icon
} from '@chakra-ui/react';
import { SearchIcon } from '@chakra-ui/icons';

/**
 * Interface defining the props for the SearchInput component.
 */
interface SearchInputProps {
  /**
   * The current value of the search input.
   */
  value: string;
  /**
   * Callback function to handle changes in the search input value.
   */
  onChange: (value: string) => void;
  /**
   * Optional placeholder text for the search input.
   */
  placeholder?: string;
  /**
   * Optional aria-label for accessibility.
   */
  ariaLabel?: string;
}

/**
 * React functional component for rendering a search input field using Chakra UI.
 *
 * @param {SearchInputProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const SearchInput: React.FC<SearchInputProps> = ({
  value,
  onChange,
  placeholder = 'Search...',
  ariaLabel = 'Search'
}) => {
  // Color values for light/dark mode
  const inputBg = useColorModeValue('white', 'gray.800');
  const inputBorder = useColorModeValue('gray.200', 'gray.600');
  const iconColor = useColorModeValue('gray.400', 'gray.500');

  return (
    <InputGroup size="md">
      <InputLeftElement pointerEvents="none">
        <Icon as={SearchIcon} color={iconColor} />
      </InputLeftElement>
      <Input
        type="text"
        placeholder={placeholder}
        aria-label={ariaLabel}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        bg={inputBg}
        borderColor={inputBorder}
        _hover={{ borderColor: useColorModeValue('gray.300', 'gray.500') }}
        _focus={{ 
          borderColor: useColorModeValue('blue.500', 'blue.300'),
          boxShadow: useColorModeValue('0 0 0 1px blue.500', '0 0 0 1px blue.300')
        }}
      />
    </InputGroup>
  );
};

export default SearchInput;
/**
 * React component for rendering a filter dropdown menu.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  Menu,
  MenuButton,
  MenuList,
  MenuItem,
  Button,
  Icon,
  useColorModeValue,
  Flex,
  Text
} from '@chakra-ui/react';
import { ChevronDownIcon, CheckIcon } from '@chakra-ui/icons';

/**
 * Interface defining a filter option.
 */
interface FilterOption {
  /**
   * The value of the filter option.
   */
  value: string;
  /**
   * The label to display for the filter option.
   */
  label: string;
}

/**
 * Interface defining the props for the FilterDropdown component.
 */
interface FilterDropdownProps {
  /**
   * The currently selected filter value.
   */
  value: string;
  /**
   * Callback function to handle changes in the selected filter.
   */
  onChange: (value: string) => void;
  /**
   * Array of filter options to display in the dropdown.
   */
  options: FilterOption[];
  /**
   * Optional label for the filter dropdown button.
   */
  label?: string;
  /**
   * Optional placeholder text when no filter is selected.
   */
  placeholder?: string;
}

/**
 * React functional component for rendering a filter dropdown menu using Chakra UI.
 *
 * @param {FilterDropdownProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const FilterDropdown: React.FC<FilterDropdownProps> = ({
  value,
  onChange,
  options,
  label = 'Filter',
  placeholder = 'Select filter...'
}) => {
  // Color values for light/dark mode
  const buttonBg = useColorModeValue('white', 'gray.800');
  const buttonBorder = useColorModeValue('gray.200', 'gray.600');
  const menuBg = useColorModeValue('white', 'gray.800');
  const menuBorder = useColorModeValue('gray.200', 'gray.600');
  const hoverBg = useColorModeValue('gray.100', 'gray.700');
  const selectedBg = useColorModeValue('blue.50', 'blue.900');
  const selectedColor = useColorModeValue('blue.600', 'blue.200');
  const labelColor = useColorModeValue('gray.600', 'gray.400');

  // Find the selected option
  const selectedOption = options.find(option => option.value === value);

  return (
    <Menu closeOnSelect={true}>
      <MenuButton
        as={Button}
        rightIcon={<ChevronDownIcon />}
        bg={buttonBg}
        borderColor={buttonBorder}
        borderWidth="1px"
        _hover={{ bg: hoverBg }}
        _active={{ bg: hoverBg }}
        size="md"
        width="100%"
      >
        <Flex alignItems="center" justifyContent="space-between">
          {label && (
            <Text fontSize="sm" color={labelColor} mr={2}>
              {label}:
            </Text>
          )}
          <Text fontWeight="medium" isTruncated>
            {selectedOption ? selectedOption.label : placeholder}
          </Text>
        </Flex>
      </MenuButton>
      <MenuList bg={menuBg} borderColor={menuBorder} minWidth="200px">
        {options.map((option) => (
          <MenuItem
            key={option.value}
            onClick={() => onChange(option.value)}
            bg={option.value === value ? selectedBg : 'transparent'}
            color={option.value === value ? selectedColor : 'inherit'}
            _hover={{ bg: hoverBg }}
          >
            <Flex alignItems="center" width="100%">
              <Text flex="1">{option.label}</Text>
              {option.value === value && (
                <Icon as={CheckIcon} color={selectedColor} ml={2} />
              )}
            </Flex>
          </MenuItem>
        ))}
      </MenuList>
    </Menu>
  );
};

export default FilterDropdown;
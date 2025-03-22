/**
 * React component for rendering a sort dropdown menu.
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
import { ChevronDownIcon, ArrowUpIcon, ArrowDownIcon } from '@chakra-ui/icons';
import { SortConfig } from '../types';

/**
 * Interface defining a sort option.
 */
interface SortOption {
  /**
   * The key of the sort option.
   */
  key: string;
  /**
   * The label to display for the sort option.
   */
  label: string;
}

/**
 * Interface defining the props for the SortDropdown component.
 */
interface SortDropdownProps {
  /**
   * The current sort configuration.
   */
  sortConfig: SortConfig;
  /**
   * Callback function to handle changes in the sort configuration.
   */
  onSort: (key: string) => void;
  /**
   * Array of sort options to display in the dropdown.
   */
  options: SortOption[];
  /**
   * Optional label for the sort dropdown button.
   */
  label?: string;
}

/**
 * React functional component for rendering a sort dropdown menu using Chakra UI.
 *
 * @param {SortDropdownProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const SortDropdown: React.FC<SortDropdownProps> = ({
  sortConfig,
  onSort,
  options,
  label = 'Sort by'
}) => {
  // Color values for light/dark mode
  const buttonBg = useColorModeValue('white', 'gray.800');
  const buttonBorder = useColorModeValue('gray.200', 'gray.600');
  const menuBg = useColorModeValue('white', 'gray.800');
  const menuBorder = useColorModeValue('gray.200', 'gray.600');
  const hoverBg = useColorModeValue('gray.100', 'gray.700');
  const selectedBg = useColorModeValue('blue.50', 'blue.900');
  const selectedColor = useColorModeValue('blue.600', 'blue.200');
  const labelColor = useColorModeValue('gray.600', 'gray.300');
  const textColor = useColorModeValue('gray.800', 'gray.100');

  // Find the selected option
  const selectedOption = options.find(option => option.key === sortConfig.key);

  return (
    <Menu closeOnSelect={true}>
      <MenuButton
        as={Button}
        rightIcon={<Icon as={ChevronDownIcon} color={textColor} />}
        bg={buttonBg}
        borderColor={buttonBorder}
        borderWidth="1px"
        _hover={{ bg: hoverBg }}
        _active={{ bg: hoverBg }}
        size="md"
        width="100%"
      >
        <Flex alignItems="center" justifyContent="space-between">
          <Text fontSize="sm" color={labelColor} mr={2}>
            {label}:
          </Text>
          <Flex alignItems="center">
            <Text fontWeight="medium" mr={1} color={textColor}>
              {selectedOption ? selectedOption.label : 'Name'}
            </Text>
            {sortConfig.direction === 'asc' ? (
              <Icon as={ArrowUpIcon} boxSize={3} color={textColor} />
            ) : (
              <Icon as={ArrowDownIcon} boxSize={3} color={textColor} />
            )}
          </Flex>
        </Flex>
      </MenuButton>
      <MenuList bg={menuBg} borderColor={menuBorder} minWidth="200px">
        {options.map((option) => (
          <MenuItem
            key={option.key}
            onClick={() => onSort(option.key)}
            bg={option.key === sortConfig.key ? selectedBg : 'transparent'}
            color={option.key === sortConfig.key ? selectedColor : textColor}
            _hover={{ bg: hoverBg }}
          >
            <Flex alignItems="center" width="100%">
              <Text flex="1" color={option.key === sortConfig.key ? selectedColor : textColor}>{option.label}</Text>
              {option.key === sortConfig.key && (
                <Icon 
                  as={sortConfig.direction === 'asc' ? ArrowUpIcon : ArrowDownIcon} 
                  color={selectedColor} 
                  ml={2}
                  boxSize={3}
                />
              )}
            </Flex>
          </MenuItem>
        ))}
      </MenuList>
    </Menu>
  );
};

export default SortDropdown;
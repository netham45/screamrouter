/**
 * React component for a navigation item in the sidebar.
 * This component provides a consistent UI for navigation links with icons and optional badges.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  Button,
  Flex,
  Text,
  Badge,
  useColorModeValue
} from '@chakra-ui/react';

/**
 * Interface defining the props for the NavItem component.
 */
interface NavItemProps {
  /**
   * Icon name from Font Awesome (without the 'fa-' prefix).
   */
  icon: string;
  
  /**
   * Label text for the navigation item.
   */
  label: string;
  
  /**
   * Whether the item is currently active/selected.
   */
  isActive?: boolean;
  
  /**
   * Optional badge count to display (e.g., number of items).
   */
  badge?: number;
  
  /**
   * Click handler for the navigation item.
   */
  onClick: () => void;
}

/**
 * React functional component for a navigation item.
 *
 * @param {NavItemProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const NavItem: React.FC<NavItemProps> = ({
  icon,
  label,
  isActive = false,
  badge,
  onClick
}) => {
  // Define colors based on color mode and active state
  const activeBg = useColorModeValue('blue.100', 'blue.700');
  const hoverBg = useColorModeValue('gray.100', 'gray.700');
  const activeColor = useColorModeValue('blue.700', 'white');
  const color = useColorModeValue('gray.700', 'gray.200');
  const badgeBg = useColorModeValue('blue.500', 'blue.300');
  const badgeColor = useColorModeValue('white', 'gray.800');

  return (
    <Button
      variant="ghost"
      justifyContent="flex-start"
      alignItems="center"
      width="100%"
      py={3}
      px={4}
      borderRadius="md"
      fontWeight="normal"
      bg={isActive ? activeBg : 'transparent'}
      color={isActive ? activeColor : color}
      _hover={{ bg: isActive ? activeBg : hoverBg }}
      onClick={onClick}
      aria-current={isActive ? 'page' : undefined}
      leftIcon={<i className={`fas fa-${icon}`} aria-hidden="true"></i>}
    >
      <Flex width="100%" justify="space-between" align="center">
        <Text>{label}</Text>
        {badge !== undefined && (
          <Badge
            ml={2}
            borderRadius="full"
            px={2}
            bg={badgeBg}
            color={badgeColor}
          >
            {badge}
          </Badge>
        )}
      </Flex>
    </Button>
  );
};

export default NavItem;
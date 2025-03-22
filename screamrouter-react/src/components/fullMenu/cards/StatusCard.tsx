/**
 * React component for displaying a status card.
 * This component provides a consistent UI for displaying status information in the dashboard.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  Box,
  Flex,
  Heading,
  Text,
  Badge,
  useColorModeValue
} from '@chakra-ui/react';

/**
 * Interface defining the props for the StatusCard component.
 */
interface StatusCardProps {
  /**
   * Title of the status card.
   */
  title: string;
  
  /**
   * Icon name from Font Awesome (without the 'fa-' prefix).
   */
  icon: string;
  
  /**
   * Optional subtitle or description.
   */
  subtitle?: string;
  
  /**
   * Optional status text (e.g., "Active", "Inactive").
   */
  status?: string;
  
  /**
   * Whether the status is positive (e.g., "Active", "Connected").
   */
  isPositive?: boolean;
  
  /**
   * Children elements to render inside the card.
   */
  children?: React.ReactNode;
  
  /**
   * Optional click handler for the entire card.
   */
  onClick?: () => void;
}

/**
 * React functional component for a status card.
 * 
 * @param {StatusCardProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const StatusCard: React.FC<StatusCardProps> = ({ 
  title, 
  icon, 
  subtitle, 
  status, 
  isPositive = true, 
  children, 
  onClick 
}) => {
  // Define colors based on color mode
  const cardBg = useColorModeValue('white', 'gray.800');
  const cardBorderColor = useColorModeValue('gray.200', 'gray.700');
  const cardHoverBg = useColorModeValue('gray.50', 'gray.700');
  const titleColor = useColorModeValue('gray.800', 'white');
  const subtitleColor = useColorModeValue('gray.600', 'gray.400');

  return (
    <Box
      p={4}
      borderWidth="1px"
      borderRadius="lg"
      borderColor={cardBorderColor}
      bg={cardBg}
      boxShadow="sm"
      transition="all 0.2s"
      _hover={onClick ? { boxShadow: 'md', bg: cardHoverBg } : {}}
      onClick={onClick}
      role={onClick ? 'button' : undefined}
      tabIndex={onClick ? 0 : undefined}
      cursor={onClick ? 'pointer' : 'default'}
    >
      <Flex justify="space-between" align="center" mb={subtitle ? 2 : 0}>
        <Flex align="center">
          <Box as="i" className={`fas fa-${icon}`} fontSize="xl" color={titleColor} mr={2} aria-hidden="true" />
          <Heading as="h3" size="md" color={titleColor}>
            {title}
          </Heading>
        </Flex>
        {status && (
          <Badge
            colorScheme={isPositive ? 'green' : 'red'}
            px={2}
            py={1}
            borderRadius="full"
          >
            {status}
          </Badge>
        )}
      </Flex>
      
      {subtitle && (
        <Text color={subtitleColor} fontSize="sm" mb={children ? 4 : 0}>
          {subtitle}
        </Text>
      )}
      
      {children && (
        <Box mt={2}>
          {children}
        </Box>
      )}
    </Box>
  );
};

export default StatusCard;
import React from 'react';
import { Box, VStack, Heading, Text, Button, useColorModeValue } from '@chakra-ui/react';
import { EmptyStateProps } from '../types';

/**
 * EmptyState component for the FullMenu.
 * This component displays a message when there is no content to show.
 * Uses Chakra UI components for consistent styling.
 */
const EmptyState: React.FC<EmptyStateProps> = ({
  icon,
  title,
  message,
  actionText,
  onAction
}) => {
  // Define colors based on color mode
  const bgColor = useColorModeValue('gray.50', 'gray.700');
  const borderColor = useColorModeValue('gray.200', 'gray.600');
  const iconColor = useColorModeValue('blue.500', 'blue.300');

  return (
    <Box
      p={8}
      borderWidth="1px"
      borderRadius="lg"
      borderColor={borderColor}
      bg={bgColor}
      textAlign="center"
      width="100%"
      maxWidth="500px"
      mx="auto"
      my={8}
    >
      <VStack spacing={4}>
        {/* Using a generic InfoIcon as fallback if icon name is not recognized */}
        <Box fontSize="4xl" color={iconColor}>
          <i className={`fas fa-${icon}`}></i>
        </Box>
        <Heading as="h3" size="lg">
          {title}
        </Heading>
        <Text color={useColorModeValue('gray.600', 'gray.300')}>
          {message}
        </Text>
        {actionText && onAction && (
          <Button
            colorScheme="blue"
            onClick={onAction}
            mt={2}
          >
            {actionText}
          </Button>
        )}
      </VStack>
    </Box>
  );
};

export default EmptyState;
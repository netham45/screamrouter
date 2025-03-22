/**
 * React component for rendering a button that expands and collapses a list of routes.
 * It includes both active and disabled routes and can navigate to a specific route if used in the desktop menu.
 * Uses Chakra UI components for consistent styling.
 */
import React, { useState } from 'react';
import {
  Box,
  Heading,
  List,
  ListItem,
  Collapse,
  useColorModeValue
} from '@chakra-ui/react';
import { Route } from '../../../api/api';
import ActionButton from './ActionButton';

/**
 * Interface defining the props for the ExpandRoutesButton component.
 */
interface ExpandRoutesButtonProps {
  /**
   * Array of active routes.
   */
  activeRoutes: Route[];
  /**
   * Array of disabled routes.
   */
  disabledRoutes: Route[];
  /**
   * Optional boolean indicating if the button is used in the desktop menu.
   */
  isDesktopMenu?: boolean;
  /**
   * Optional callback function to navigate to a specific route when clicked.
   */
  onNavigate?: (type: 'routes', itemName: string) => void;
}

/**
 * React functional component for rendering an expand/collapse button for routes.
 *
 * @param {ExpandRoutesButtonProps} props - The props passed to the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ExpandRoutesButton: React.FC<ExpandRoutesButtonProps> = ({ activeRoutes, disabledRoutes, isDesktopMenu, onNavigate }) => {
  /**
   * State variable indicating whether the routes list is expanded or collapsed.
   */
  const [isExpanded, setIsExpanded] = useState(false);
  
  // Color values for light/dark mode
  const bgColor = useColorModeValue('gray.50', 'gray.700');
  const borderColor = useColorModeValue('gray.200', 'gray.600');

  /**
   * Function to toggle the expansion state of the routes list.
   */
  const toggleExpand = () => {
    setIsExpanded(!isExpanded);
  };

  /**
   * Function to handle clicking on a route name.
   *
   * @param {string} routeName - The name of the clicked route.
   */
  const handleRouteClick = (routeName: string) => {
    if (isDesktopMenu && onNavigate) {
      onNavigate('routes', routeName);
    }
  };

  return (
    <Box>
      <ActionButton onClick={toggleExpand}>
        {isExpanded ? 'Hide Routes' : 'Show Routes'}
      </ActionButton>
      
      <Collapse in={isExpanded} animateOpacity>
        <Box
          mt={2}
          p={3}
          bg={bgColor}
          borderRadius="md"
          borderWidth="1px"
          borderColor={borderColor}
        >
          <Heading as="h4" size="sm" mb={2}>
            Active Routes:
          </Heading>
          <List spacing={1}>
            {activeRoutes.map(route => (
              <ListItem
                key={route.name}
                onClick={() => handleRouteClick(route.name)}
                cursor={isDesktopMenu ? 'pointer' : 'default'}
                p={1}
                _hover={isDesktopMenu ? { bg: 'gray.100' } : {}}
                borderRadius="sm"
              >
                {route.name}
              </ListItem>
            ))}
          </List>
          
          <Heading as="h4" size="sm" mt={3} mb={2}>
            Disabled Routes:
          </Heading>
          <List spacing={1}>
            {disabledRoutes.map(route => (
              <ListItem
                key={route.name}
                onClick={() => handleRouteClick(route.name)}
                cursor={isDesktopMenu ? 'pointer' : 'default'}
                p={1}
                _hover={isDesktopMenu ? { bg: 'gray.100' } : {}}
                borderRadius="sm"
                opacity={0.7}
              >
                {route.name}
              </ListItem>
            ))}
          </List>
        </Box>
      </Collapse>
    </Box>
  );
};

export default ExpandRoutesButton;

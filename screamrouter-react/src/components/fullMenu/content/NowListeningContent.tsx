import React from 'react';
import {
  Box,
  Heading,
  Flex,
  Text,
  Button,
  Badge,
  SimpleGrid,
  Divider,
  Icon,
  useColorModeValue
} from '@chakra-ui/react';
import { StarIcon } from '@chakra-ui/icons';
import { FaSlidersH, FaRoute, FaChartBar, FaPlus } from 'react-icons/fa';
import { ContentProps } from '../types';
import EmptyState from './EmptyState';
import ResourceCard from '../cards/ResourceCard';
import { openAddRouteWithPreselection } from '../utils';

/**
 * NowListeningContent component for the FullMenu.
 * This component displays detailed information about the sink being listened to.
 */
const NowListeningContent: React.FC<ContentProps> = ({
  routes,
  starredSinks,
  listeningToSink,
  setCurrentCategory,
  handleStar,
  handleToggleSink,
  handleToggleRoute,
  handleOpenSinkEqualizer,
  handleOpenVisualizer,
  handleUpdateRouteVolume,
  handleUpdateRouteTimeshift,
  actions
}) => {
  // Define colors based on color mode
  const cardBg = useColorModeValue('white', 'gray.800');
  const cardBorderColor = useColorModeValue('gray.200', 'gray.700');
  const headingColor = useColorModeValue('gray.700', 'white');
  const labelColor = useColorModeValue('gray.600', 'gray.400');
  const valueColor = useColorModeValue('gray.800', 'white');
  const buttonHoverBg = useColorModeValue('gray.200', 'gray.600');
  const actionButtonBg = useColorModeValue('blue.50', 'blue.900');
  const actionButtonColor = useColorModeValue('blue.600', 'blue.200');
  const starColor = useColorModeValue('yellow.400', 'yellow.300');

  if (!listeningToSink) {
    return (
      <EmptyState
        icon="headphones"
        title="Not Listening to Any Sink"
        message="Select a sink to listen to"
        actionText="View Sinks"
        onAction={() => setCurrentCategory('sinks')}
      />
    );
  }
  
  return (
    <Box>
      <Box
        p={5}
        borderWidth="1px"
        borderRadius="lg"
        borderColor={cardBorderColor}
        bg={cardBg}
        mb={6}
        boxShadow="sm"
      >
        <Flex justify="space-between" align="center" mb={4}>
          <Heading as="h2" size="lg" color={headingColor}>
            {listeningToSink.name}
          </Heading>
          <Button
            variant="ghost"
            onClick={() => handleStar('sinks', listeningToSink.name)}
            aria-label={starredSinks.includes(listeningToSink.name) ? 'Unstar' : 'Star'}
            color={starredSinks.includes(listeningToSink.name) ? starColor : 'gray.400'}
            _hover={{ bg: 'transparent' }}
          >
            <Icon as={StarIcon} boxSize={5} />
          </Button>
        </Flex>
        
        <Divider mb={4} />
        
        <SimpleGrid columns={{ base: 1, md: 2 }} spacing={4} mb={6}>
          <Flex direction="column">
            <Text fontWeight="bold" color={labelColor} mb={1}>Status</Text>
            <Badge
              colorScheme={listeningToSink.enabled ? 'green' : 'red'}
              alignSelf="flex-start"
              px={2}
              py={1}
              borderRadius="full"
            >
              {listeningToSink.enabled ? 'Enabled' : 'Disabled'}
            </Badge>
          </Flex>
          
          {listeningToSink.ip && (
            <Flex direction="column">
              <Text fontWeight="bold" color={labelColor} mb={1}>IP Address</Text>
              <Text color={valueColor}>{listeningToSink.ip}</Text>
            </Flex>
          )}
          
          {listeningToSink.port && (
            <Flex direction="column">
              <Text fontWeight="bold" color={labelColor} mb={1}>Port</Text>
              <Text color={valueColor}>{listeningToSink.port}</Text>
            </Flex>
          )}
          
          {listeningToSink.volume !== undefined && (
            <Flex direction="column">
              <Text fontWeight="bold" color={labelColor} mb={1}>Volume</Text>
              <Text color={valueColor}>{listeningToSink.volume}%</Text>
            </Flex>
          )}
        </SimpleGrid>
        
        <Flex wrap="wrap" gap={3}>
          <Button
            colorScheme={listeningToSink.enabled ? 'red' : 'green'}
            onClick={() => handleToggleSink(listeningToSink.name)}
            size="md"
            mr={2}
          >
            {listeningToSink.enabled ? 'Disable' : 'Enable'}
          </Button>
          
          <Button
            leftIcon={<i className="fas fa-headphones-alt"></i>}
            colorScheme="teal"
            onClick={() => actions.listenToSink(null)}
            size="md"
          >
            Stop Listening
          </Button>
          
          <Button
            leftIcon={<Icon as={FaSlidersH} />}
            onClick={() => handleOpenSinkEqualizer(listeningToSink.name)}
            bg={actionButtonBg}
            color={actionButtonColor}
            _hover={{ bg: buttonHoverBg }}
            size="md"
          >
            Equalizer
          </Button>
          
          {handleOpenVisualizer && (
            <Button
              leftIcon={<Icon as={FaChartBar} />}
              onClick={() => handleOpenVisualizer(listeningToSink)}
              bg={actionButtonBg}
              color={actionButtonColor}
              _hover={{ bg: buttonHoverBg }}
              size="md"
            >
              Visualizer
            </Button>
          )}
        </Flex>
      </Box>
      
      <Box mb={6}>
        <Flex align="center" justify="space-between" mb={4}>
          <Flex align="center">
            <Icon as={FaRoute} mr={2} color={headingColor} />
            <Heading as="h3" size="md" color={headingColor}>
              Connected Routes
            </Heading>
          </Flex>
          <Button
            leftIcon={<Icon as={FaPlus} />}
            colorScheme="blue"
            size="sm"
            onClick={() => openAddRouteWithPreselection('sink', listeningToSink.name)}
          >
            Add Route
          </Button>
        </Flex>
        
        <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={4}>
          {routes.filter(route => route.sink === listeningToSink.name).map(route => (
            <ResourceCard
              key={`sink-route-${route.name}`}
              item={route}
              type="routes"
              isStarred={false}
              isActive={route.enabled}
              onStar={() => handleStar('routes', route.name)}
              onActivate={() => handleToggleRoute(route.name)}
              onUpdateVolume={(volume) => handleUpdateRouteVolume(route.name, volume)}
              onUpdateTimeshift={(timeshift) => handleUpdateRouteTimeshift(route.name, timeshift)}
              routes={routes}
            />
          ))}
        </SimpleGrid>
      </Box>
    </Box>
  );
};

export default NowListeningContent;
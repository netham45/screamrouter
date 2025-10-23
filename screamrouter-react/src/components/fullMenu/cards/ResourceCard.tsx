/**
 * React component for displaying a resource card.
 * This component provides a consistent UI for displaying sources, sinks, and routes in a card layout.
 * Uses Chakra UI components for consistent styling.
 */
import React from 'react';
import {
  Box,
  Flex,
  Heading,
  Text,
  Button,
  IconButton,
  Badge,
  HStack,
  useColorModeValue,
  Accordion,
  AccordionItem,
  AccordionButton,
  AccordionPanel,
  AccordionIcon,
  VStack
} from '@chakra-ui/react';
import { StarIcon, SettingsIcon, DeleteIcon } from '@chakra-ui/icons';
import { FaPlay, FaStepBackward, FaStepForward, FaSlidersH, FaBroadcastTower, FaProjectDiagram, FaHeadphones, FaListUl } from 'react-icons/fa';
import { Source, Sink, Route } from '../../../api/api';
import VolumeSlider from '../controls/VolumeSlider';
import TimeshiftSlider from '../controls/TimeshiftSlider';
import { openEditPage, getProcessGroupIp } from '../utils';

/**
 * Type for the different resource types.
 */
type ResourceType = 'sources' | 'sinks' | 'routes' | 'group-source' | 'group-sink';

/**
 * Union type for all resource items.
 */
type ResourceItem = Source | Sink | Route;

/**
 * Interface defining the props for the ResourceCard component.
 */
interface ResourceCardProps {
  /**
   * The resource item to display.
   */
  item: ResourceItem;
  
  /**
   * The type of resource (sources, sinks, or routes).
   */
  type: ResourceType;
  
  /**
   * Whether the item is starred/favorited.
   */
  isStarred: boolean;
  
  /**
   * Whether the item is active (e.g., Primary Source, listening sink).
   */
  isActive: boolean;
  
  /**
   * Handler for starring/unstarring the item.
   */
  onStar: () => void;
  
  /**
   * Handler for activating/deactivating the item.
   */
  onActivate: () => void;
  
  /**
   * Handler for editing the item.
   */
  onEdit?: () => void;
  
  /**
   * Handler for opening the equalizer for the item.
   */
  onEqualizer?: () => void;
  
  /**
   * Handler for opening VNC for the item (sources only).
   */
  onVnc?: () => void;
  
  /**
   * Handler for listening to a sink (sinks only).
   */
  onListen?: () => void;
  
  /**
   * Handler for visualizing a sink (sinks only).
   */
  onVisualize?: () => void;
  
  /**
   * Handler for updating volume.
   */
  onUpdateVolume?: (volume: number) => void;
  
  /**
   * Handler for updating timeshift.
   */
  onUpdateTimeshift?: (timeshift: number) => void;
  
  /**
   * Handler for opening channel mapping dialog.
   */
  onChannelMapping?: () => void;
  
  /**
   * Handler for deleting the item.
   */
  onDelete?: () => void;
  
  /**
   * All routes in the system, used to determine active and disabled routes
   */
  routes?: Route[];
  
  /**
   * All sources in the system, used to determine group memberships
   */
  allSources?: Source[];
  
  /**
   * All sinks in the system, used to determine group memberships
   */
  allSinks?: Sink[];
  
  /**
   * Navigation function to navigate to a specific item
   */
  navigate?: (type: ResourceType, name: string) => void;
  
  /**
   * Handler for toggling Primary Source (sources only).
   */
  onToggleActiveSource?: () => void;
  
  /**
   * Handler for controlling source playback (sources with VNC only).
   */
  onControlSource?: (action: 'prevtrack' | 'play' | 'nexttrack') => void;
  
  /**
   * Function to directly navigate to the details page of this item.
   * If provided, the "View Details" button will use this instead of the regular navigate function.
   */
  navigateToDetails?: () => void;
}

/**
 * React functional component for a resource card.
 * 
 * @param {ResourceCardProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ResourceCard: React.FC<ResourceCardProps> = ({
  item,
  type,
  isStarred,
  isActive,
  onStar,
  onActivate,
  onEdit,
  onEqualizer,
  onVnc,
  onListen,
  onVisualize,
  onUpdateVolume,
  onUpdateTimeshift,
  onChannelMapping,
  onDelete,
  routes = [],
  allSources = [],
  allSinks = [],
  navigate,
  onToggleActiveSource,
  onControlSource,
  navigateToDetails
}) => {
  // Type guards to check the type of the item
  const isSource = (item: ResourceItem): item is Source => type === 'sources';
  const isSink = (item: ResourceItem): item is Sink => type === 'sinks';
  const isRoute = (item: ResourceItem): item is Route => type === 'routes';

  // Determine whether this source represents a process group
  const processGroupIp = isSource(item) ? getProcessGroupIp(item, allSources) : null;

  // Function to get active routes for a source or sink
  const getActiveRoutes = (): string[] => {
    if (isSource(item)) {
      return routes
        .filter(route => route.source === item.name && route.enabled)
        .map(route => route.name);
    } else if (isSink(item)) {
      return routes
        .filter(route => route.sink === item.name && route.enabled)
        .map(route => route.name);
    }
    return [];
  };

  // Function to get disabled routes for a source or sink
  const getDisabledRoutes = (): string[] => {
    if (isSource(item)) {
      return routes
        .filter(route => route.source === item.name && !route.enabled)
        .map(route => route.name);
    } else if (isSink(item)) {
      return routes
        .filter(route => route.sink === item.name && !route.enabled)
        .map(route => route.name);
    }
    return [];
  };

  // Get active and disabled routes
  const activeRoutes = getActiveRoutes();
  const disabledRoutes = getDisabledRoutes();
  
  // Function to find groups that this item belongs to
  const findParentGroups = (): { name: string, type: 'sources' | 'sinks' }[] => {
    const parentGroups: { name: string, type: 'sources' | 'sinks' }[] = [];
    
    if (isSource(item)) {
      // Find source groups that contain this source
      allSources.forEach(source => {
        if (source.is_group && source.group_members && source.group_members.includes(item.name)) {
          parentGroups.push({ name: source.name, type: 'sources' });
        }
      });
    } else if (isSink(item)) {
      // Find sink groups that contain this sink
      allSinks.forEach(sink => {
        if (sink.is_group && sink.group_members && sink.group_members.includes(item.name)) {
          parentGroups.push({ name: sink.name, type: 'sinks' });
        }
      });
    }
    
    return parentGroups;
  };
  
  // Get parent groups
  const parentGroups = findParentGroups();

  // Define colors based on color mode
  const cardBg = useColorModeValue('white', 'gray.800');
  const headingColor = useColorModeValue('gray.800', 'white');
  const textColor = useColorModeValue('gray.600', 'gray.300');
  
  // Define state colors
  const starredColor = useColorModeValue('yellow.400', 'yellow.500');      // Favorite: Yellow
  
  // Handle card click to navigate to detail view
  const handleCardClick = () => {
    if (navigate) {
      if (isSource(item)) {
        navigate('sources', item.name);
      } else if (isSink(item)) {
        navigate('sinks', item.name);
      } else if (isRoute(item)) {
        navigate('routes', item.name);
      } else {
        console.log("I don't know what type of card this is");
      }
    }
  };
  
  // No longer using a helper function for badges

return (
  <Box
    id={`${type.slice(0, -1)}-${item.name}`}
    borderWidth="1px"
    borderRadius="lg"
    overflow="hidden"
    bg={cardBg}
    boxShadow="md"
    transition="all 0.2s"
    position="relative"
    pr={2}
  >
    {/* Header section - always visible */}
    <Box pt={5} pl={5}>
      <Flex justify="space-between" align="flex-start" mb={0}>
        <VStack>
        <Heading
          as="h3"
          size="md"
          color={headingColor}
          cursor={navigate ? "pointer" : "default"}
          _hover={navigate ? { color: useColorModeValue('blue.600', 'blue.300') } : {}}
          onClick={navigateToDetails || handleCardClick}
          flex="1"
          mr={2}
          minW="0"
        >
          {item.name}
        </Heading>
        <Flex wrap="wrap" justify="flex-end" minW="fit-content">
          <HStack justify="flex-start" align="flex-start" wrap="wrap">
          <IconButton
            aria-label={isStarred ? 'Remove from favorites' : 'Add to favorites'}
            title={isStarred ? 'Remove from favorites' : 'Add to favorites'}
            icon={<StarIcon color={isStarred ? starredColor : 'gray.400'} />}
            onClick={onStar}
            variant="ghost"
            size="sm"
            data-tutorial-id="resource-favorite-toggle"
          />
          {isSource(item) && (
            <IconButton
              aria-label={isActive ? 'Unmark as Primary Source' : 'Mark as Primary Source'}
              title={isActive ? 'Unmark as Primary Source' : 'Mark as Primary Source'}
              icon={
                <Box
                  as={FaBroadcastTower}
                  color={isActive ? 'cyan.500' : 'gray.400'}
                  transition="all 0.2s"
                />
              }
              onClick={(e) => {
                e.stopPropagation();
                if (onToggleActiveSource) {
                  onToggleActiveSource();
                }
              }}
              variant="ghost"
              size="sm"
              data-tutorial-id="resource-primary-toggle"
            />
          )}
          {onChannelMapping && (
            <IconButton
              aria-label="Channel Mapping"
              title="Open Channel Mapping"
              icon={<Box as={FaProjectDiagram} />}
              onClick={onChannelMapping}
              variant="ghost"
              size="sm"
              data-tutorial-id="resource-channel-mapping-button"
            />
          )}
          {isSource(item) && processGroupIp && (
            <IconButton
              aria-label="View Processes"
              title="View Processes"
              icon={<Box as={FaListUl} />}
              onClick={(e) => {
                e.stopPropagation();
                window.open(
  `/site/processes/${processGroupIp}`,
  'process-window',
  'popup=yes,noopener,noreferrer,toolbar=no,location=no,menubar=no,status=no,scrollbars=yes,resizable=yes,width=900,height=700'
);
              }}
              variant="ghost"
              size="sm"
            />
          )}
          {onEqualizer && (
            <IconButton
              aria-label="Equalizer"
              title="Open Equalizer"
              icon={<Box as={FaSlidersH} transform="rotate(90deg)" />}
              onClick={onEqualizer}
              variant="ghost"
              size="sm"
              data-tutorial-id="resource-equalizer-button"
            />
          )}
          <IconButton
            aria-label="Edit"
            title="Edit"
            icon={<SettingsIcon />}
            onClick={() => {
              if (onEdit) {
                onEdit();
              } else {
                // Use the openEditPage utility function to open the edit page in a new window
                if (isSource(item)) {
                  openEditPage('sources', item);
                } else if (isSink(item)) {
                  openEditPage('sinks', item);
                } else if (isRoute(item)) {
                  openEditPage('routes', item);
                }
              }
            }}
            variant="ghost"
            size="sm"
          />
          {onDelete && (
            <IconButton
              aria-label="Delete"
              title="Delete"
              icon={<DeleteIcon />}
              onClick={() => {
                if (window.confirm(`Are you sure you want to delete ${item.name}?`)) {
                  onDelete();
                }
              }}
              variant="ghost"
              size="sm"
              colorScheme="red"
            />
          )}
          </HStack>
        </Flex>
        </VStack>
      </Flex>
    </Box>
    
    {/* Badges section - always visible */}
    <Box px={5} pt={1}>
      <Flex mb={2} justify="flex-start" flexWrap="wrap" gap={1}>
        {/* Type badge - always first */}
        {isSource(item) && (
          <Badge colorScheme="green" mr={1} borderRadius="full" px={2} py={1} userSelect="none">
            Source
          </Badge>
        )}
        {isSink(item) && (
          <Badge colorScheme="blue" mr={1} borderRadius="full" px={2} py={1} userSelect="none">
            Sink
          </Badge>
        )}
        {isRoute(item) && (
          <Badge colorScheme="red" mr={1} borderRadius="full" px={2} py={1} userSelect="none">
            Route
          </Badge>
        )}
        
        {/* Enabled/disabled badge - always second and clickable */}
        <Badge
          colorScheme={item.enabled ? 'green' : 'red'}
          px={2}
          py={1}
          borderRadius="full"
          cursor="pointer"
          userSelect="none"
          _hover={{ opacity: 0.8 }}
          onClick={onActivate}
          title={item.enabled ? 'Click to disable' : 'Click to enable'}
          data-tutorial-id="resource-enable-toggle"
        >
          {item.enabled ? 'Enabled' : 'Disabled'}
        </Badge>
        
        {onListen && (
          <Badge
            colorScheme="teal"
            mr={1}
            borderRadius="full"
            px={2}
            py={1}
            userSelect="none"
            cursor="pointer"
            _hover={{ opacity: 0.8 }}
            onClick={onListen}
            title="Click to open listen page"
            data-tutorial-id="resource-listen-control"
          >
            Listen
          </Badge>
        )}
        
        {/* VNC badge for sources with VNC capabilities */}
        {isSource(item) && item.vnc_ip && onVnc && (
          <Badge
            colorScheme="purple"
            mr={1}
            borderRadius="full"
            px={2}
            py={1}
            userSelect="none"
            cursor="pointer"
            _hover={{ opacity: 0.8 }}
            onClick={onVnc}
            title="Open VNC"
          >
            VNC
          </Badge>
        )}
      </Flex>
      
      {/* Volume control - always visible */}
      {(isSource(item) || isSink(item) || isRoute(item)) && (
        <Box mt={3} mb={3}>
          <Flex mb={1} align="center">
            <Box as="i" className="fas fa-volume-up" mr={2} />
            <Text fontWeight="medium">Volume:</Text>
          </Flex>
          <VolumeSlider
            value={isRoute(item) ? (item.volume || 100) : (item.volume)}
            onChange={(value) => onUpdateVolume && onUpdateVolume(value)}
            dataTutorialId="resource-volume-slider"
          />
        </Box>
      )}
    </Box>

    <Flex justify="center" wrap="wrap" width="100%" gap={2} mb={3} minH="45px" align="center">
      {isSource(item) && item.vnc_ip && onControlSource && (
        <Flex gap={1} wrap="wrap" justify="center">
          <IconButton
            aria-label="Previous Track"
            icon={<FaStepBackward />}
            size="sm"
            colorScheme="purple"
            variant="outline"
            onClick={(e) => { e.stopPropagation(); onControlSource('prevtrack'); }}
          />
          <IconButton
            aria-label="Play/Pause"
            icon={<FaPlay />}
            size="sm"
            colorScheme="purple"
            variant="outline"
            onClick={(e) => { e.stopPropagation(); onControlSource('play'); }}
          />
          <IconButton
            aria-label="Next Track"
            icon={<FaStepForward />}
            size="sm"
            colorScheme="purple"
            variant="outline"
            onClick={(e) => { e.stopPropagation(); onControlSource('nexttrack'); }}
          />
        </Flex>
      )}
      
      {isSink(item) && onVisualize && (
        <Button
          leftIcon={<Box as="i" className="fas fa-chart-bar" />}
          colorScheme="cyan"
          variant="outline"
          size="sm"
          onClick={onVisualize}
          aria-label="Visualize"
          title="Visualize"
        >
          Visualize
        </Button>
      )}
    </Flex>
    
    {/* Accordion sections */}
    <Accordion allowMultiple defaultIndex={[]} mt={2}>

      {/* Controls Section */}
      <AccordionItem border="none">
        <h2>
          <AccordionButton
            px={4}
            py={2}
            _hover={{ bg: useColorModeValue('blue.50', 'gray.700') }}
            borderRadius="md"
            color={useColorModeValue('gray.700', 'gray.200')}
          >
            <Box flex="1" textAlign="left" fontWeight="medium">
              Timeshift
            </Box>
            <AccordionIcon color={useColorModeValue('blue.500', 'blue.300')} />
          </AccordionButton>
        </h2>
        <AccordionPanel pb={4} px={5} bg={useColorModeValue('gray.50', 'gray.800')}>
          {/* Timeshift control */}
          {(isSource(item) || isSink(item) || isRoute(item)) && (
            <Box mb={4}>
              <Flex mb={1} align="center">
                <Box as="i" className="fas fa-clock" mr={2} />
                <Text fontWeight="medium">Timeshift:</Text>
              </Flex>
              <TimeshiftSlider
                value={isSource(item) ? (item.timeshift || 0) : (isSink(item) ? (item.timeshift || 0) : (item.timeshift || 0))}
                onChange={(value) => onUpdateTimeshift && onUpdateTimeshift(value)}
              />
            </Box>
          )}
          
          {/* Visualize button for sinks */}
          {isSink(item) && onVisualize && (
            <Box mb={4}>
              <Button
                leftIcon={<Box as="i" className="fas fa-chart-bar" />}
                colorScheme="cyan"
                variant="outline"
                size="sm"
                onClick={onVisualize}
                aria-label="Visualize"
                title="Visualize"
              >
                Visualize
              </Button>
            </Box>
          )}
        </AccordionPanel>
      </AccordionItem>

      {/* Related Items Section */}
      <AccordionItem border="none">
        <h2>
          <AccordionButton
            px={4}
            py={2}
            _hover={{ bg: useColorModeValue('blue.50', 'gray.700') }}
            borderRadius="md"
            color={useColorModeValue('gray.700', 'gray.200')}
          >
            <Box flex="1" textAlign="left" fontWeight="medium">
              {isRoute(item) ? "Related Items" : "Related Routes"}
            </Box>
            <AccordionIcon color={useColorModeValue('blue.500', 'blue.300')} />
          </AccordionButton>
        </h2>
        <AccordionPanel pb={4} px={5} bg={useColorModeValue('gray.50', 'gray.800')}>
          {/* Display source and sink for routes */}
          {isRoute(item) && (
            <Box fontSize="sm">
              <Flex mb={2} align="center">
                <Box as="i" className="fas fa-microphone" mr={2} />
                <Text fontWeight="medium" mr={1}>From:</Text>
                <Text color={textColor}>{item.source}</Text>
              </Flex>
              
              <Flex mb={3} align="center">
                <Box as="i" className="fas fa-volume-up" mr={2} />
                <Text fontWeight="medium" mr={1}>To:</Text>
                <Text color={textColor}>{item.sink}</Text>
              </Flex>
              
              <Text fontWeight="semibold" fontSize="xs" color={useColorModeValue("gray.600", "gray.400")}>
                Connection Links:
              </Text>
              <Flex flexWrap="wrap" gap={1} mt={1} mb={3}>
                <Badge
                  colorScheme="green"
                  cursor={navigate ? "pointer" : "default"}
                  _hover={navigate ? { bg: "green.500" } : {}}
                  onClick={(e) => {
                    e.stopPropagation();
                    if (navigate) {
                      navigate('sources', item.source);
                    }
                  }}
                  borderRadius="full"
                  px={2}
                  py={1}
                  userSelect="none"
                >
                  Source: {item.source}
                  {navigate && (
                    <Box as="span" ml={1}>
                      <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                    </Box>
                  )}
                </Badge>
                
                <Badge
                  colorScheme="blue"
                  cursor={navigate ? "pointer" : "default"}
                  _hover={navigate ? { bg: "blue.500" } : {}}
                  onClick={(e) => {
                    e.stopPropagation();
                    if (navigate) {
                      navigate('sinks', item.sink);
                    }
                  }}
                  borderRadius="full"
                  px={2}
                  py={1}
                  userSelect="none"
                >
                  Sink: {item.sink}
                  {navigate && (
                    <Box as="span" ml={1}>
                      <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                    </Box>
                  )}
                </Badge>
              </Flex>
            </Box>
          )}
          
          {/* Display active and disabled routes for sources and sinks */}
          {(isSource(item) || isSink(item)) && (
            <Box fontSize="sm">
              {/* Display group members for group items */}
              {(isSource(item) || isSink(item)) && item.is_group && item.group_members && item.group_members.length > 0 && (
                <Box mb={2}>
                  <Text fontWeight="semibold" fontSize="xs" color={useColorModeValue("gray.600", "gray.400")}>
                    Group Members ({item.group_members.length}):
                  </Text>
                  <Flex flexWrap="wrap" gap={1} mt={1}>
                    {item.group_members.map((member, index) => (
                      <Badge
                        key={index}
                        colorScheme="purple"
                        cursor={navigate ? "pointer" : "default"}
                        _hover={navigate ? { bg: "purple.500" } : {}}
                        onClick={(e) => {
                          e.stopPropagation();
                          if (navigate) {
                            navigate(isSource(item) ? 'sources' : 'sinks', member);
                          }
                        }}
                        borderRadius="full"
                        px={2}
                        py={1}
                        userSelect="none"
                      >
                        {member}
                        {navigate && (
                          <Box as="span" ml={1}>
                            <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                          </Box>
                        )}
                      </Badge>
                    ))}
                  </Flex>
                </Box>
              )}
              
              {/* Display parent groups for non-group items */}
              {(isSource(item) || isSink(item)) && !item.is_group && parentGroups.length > 0 && (
                <Box mb={2}>
                  <Text fontWeight="semibold" fontSize="xs" color={useColorModeValue("gray.600", "gray.400")}>
                    Member of Groups ({parentGroups.length}):
                  </Text>
                  <Flex flexWrap="wrap" gap={1} mt={1}>
                    {parentGroups.map((group, index) => (
                      <Badge
                        key={index}
                        colorScheme={group.type === 'sources' ? "green" : "blue"}
                        cursor={navigate ? "pointer" : "default"}
                        _hover={navigate ? { bg: group.type === 'sources' ? "green.500" : "blue.500" } : {}}
                        onClick={(e) => {
                          e.stopPropagation();
                          if (navigate) {
                            navigate(group.type === 'sources' ? 'sources' : 'sinks', group.name);
                          }
                        }}
                        borderRadius="full"
                        px={2}
                        py={1}
                        userSelect="none"
                      >
                        {group.name}
                        {navigate && (
                          <Box as="span" ml={1}>
                            <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                          </Box>
                        )}
                      </Badge>
                    ))}
                  </Flex>
                </Box>
              )}
              
              {activeRoutes.length > 0 && (
                <Box mb={2}>
                  <Text fontWeight="semibold" fontSize="xs" color={useColorModeValue("gray.600", "gray.400")}>
                    Active Routes ({activeRoutes.length}):
                  </Text>
                  <Flex flexWrap="wrap" gap={1} mt={1}>
                    {activeRoutes.map((route, index) => (
                      <Badge
                        key={index}
                        colorScheme="green"
                        cursor={navigate ? "pointer" : "default"}
                        _hover={navigate ? { bg: "green.500" } : {}}
                        onClick={(e) => {
                          e.stopPropagation();
                          if (navigate) {
                            navigate('routes', route);
                          }
                        }}
                        borderRadius="full"
                        px={2}
                        py={1}
                        userSelect="none"
                      >
                        {route}
                        {navigate && (
                          <Box as="span" ml={1}>
                            <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                          </Box>
                        )}
                      </Badge>
                    ))}
                  </Flex>
                </Box>
              )}
              
              {disabledRoutes.length > 0 && (
                <Box>
                  <Text fontWeight="semibold" fontSize="xs" color={useColorModeValue("gray.600", "gray.400")}>
                    Disabled Routes ({disabledRoutes.length}):
                  </Text>
                  <Flex flexWrap="wrap" gap={1} mt={1}>
                    {disabledRoutes.map((route, index) => (
                      <Badge
                        key={index}
                        colorScheme="red"
                        variant="outline"
                        cursor={navigate ? "pointer" : "default"}
                        _hover={navigate ? { bg: "red.100" } : {}}
                        onClick={(e) => {
                          e.stopPropagation();
                          if (navigate) {
                            navigate('routes', route);
                          }
                        }}
                        borderRadius="full"
                        px={2}
                        py={1}
                        userSelect="none"
                      >
                        {route}
                        {navigate && (
                          <Box as="span" ml={1}>
                            <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                          </Box>
                        )}
                      </Badge>
                    ))}
                  </Flex>
                </Box>
              )}
            </Box>
          )}
        </AccordionPanel>
      </AccordionItem>
    </Accordion>
  </Box>
);
};

export default ResourceCard;

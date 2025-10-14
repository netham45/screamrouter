 /**
  * React component for displaying a resource in a list format.
  * This component provides a consistent UI for displaying sources, sinks, and routes in a list layout.
  * Uses Chakra UI components for consistent styling.
  */
 import React, { useState } from 'react';
 import {
   Box,
   Flex,
   Text,
   Button,
   IconButton,
   Badge,
   Collapse,
   HStack,
   VStack,
   useColorModeValue,
   SimpleGrid,
   Accordion,
   AccordionItem,
   AccordionButton,
   AccordionPanel,
   AccordionIcon
 } from '@chakra-ui/react';
 import { ChevronDownIcon, ChevronUpIcon, StarIcon, SettingsIcon } from '@chakra-ui/icons';
 import { FaPlay, FaStepBackward, FaStepForward, FaSlidersH, FaBroadcastTower, FaHeadphones } from 'react-icons/fa';
 import { Source, Sink, Route } from '../../../api/api';
 import VolumeSlider from '../controls/VolumeSlider';
 import TimeshiftSlider from '../controls/TimeshiftSlider';
 import { openEditPage } from '../utils';
 
 /**
  * Type for the different resource types.
  */
 type ResourceType = 'sources' | 'sinks' | 'routes' | 'group-source' | 'group-sink';
 
 /**
  * Union type for all resource items.
  */
 type ResourceItem = Source | Sink | Route;

/**
 * Interface defining the props for the ResourceListItem component.
 */
interface ResourceListItemProps {
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
 * React functional component for a resource list item.
 * 
 * @param {ResourceListItemProps} props - The properties for the component.
 * @returns {JSX.Element} The rendered JSX element.
 */
const ResourceListItem: React.FC<ResourceListItemProps> = ({
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
  routes = [],
  allSources = [],
  allSinks = [],
  navigate,
  onToggleActiveSource,
  onControlSource,
  navigateToDetails
}) => {
  const [expanded, setExpanded] = useState(false);
  
  const toggleExpand = () => {
    setExpanded(!expanded);
  };
  
  // Handle item click to navigate to detail view
  const handleItemClick = (e: React.MouseEvent) => {
    // If we're clicking on a button or icon button, don't navigate
    if ((e.target as HTMLElement).closest('button')) {
      return;
    }
    
    if (navigateToDetails) {
      navigateToDetails();
    } else if (navigate) {
      if (isSource(item)) {
        navigate('sources', item.name);
      } else if (isSink(item)) {
        navigate('sinks', item.name);
      } else if (isRoute(item)) {
        navigate('routes', item.name);
      }
    }
  };

  // Type guards to check the type of the item
  const isSource = (item: ResourceItem): item is Source => type === 'sources';
  const isSink = (item: ResourceItem): item is Sink => type === 'sinks';
  const isRoute = (item: ResourceItem): item is Route => type === 'routes';

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
  
  // Helper function to get status badges
  // Define colors based on color mode
  const cardBg = useColorModeValue('white', 'gray.800');
  const headingColor = useColorModeValue('gray.800', 'white');
  const textColor = useColorModeValue('gray.600', 'gray.300');
  
  // Define state colors
  const starredColor = useColorModeValue('yellow.400', 'yellow.500');      // Favorite: Yellow
  
  // Function to get status badges
  const getStatusBadges = () => {
    const badges = [];
    
    // Type badge
    if (isSource(item)) {
      badges.push(
        <Badge key="type" colorScheme="green" mr={1} borderRadius="full" px={2} py={1} userSelect="none">
          Source
        </Badge>
      );
    } else if (isSink(item)) {
      badges.push(
        <Badge key="type" colorScheme="blue" mr={1} borderRadius="full" px={2} py={1} userSelect="none">
          Sink
        </Badge>
      );
    } else if (isRoute(item)) {
      badges.push(
        <Badge key="type" colorScheme="red" mr={1} borderRadius="full" px={2} py={1} userSelect="none">
          Route
        </Badge>
      );
    }
    
    return badges;
  };

  return (
    <Box
      id={`${type.slice(0, -1)}-${item.name}`}
      className={`resource-list-item ${isActive ? 'active' : ''} ${expanded ? 'expanded' : ''}`}
      borderWidth="1px"
      borderRadius="md"
      mb={2}
      overflow="hidden"
      bg={cardBg}
      _hover={{ boxShadow: "sm" }}
      display="flex"
      flexDirection="column"
      width="100%"
      position="relative"
    >
      <Flex
        p={3}
        alignItems="center"
        justifyContent="space-between"
        borderBottomWidth={expanded ? "1px" : "0"}
        borderColor="transparent"
        width="100%"
        cursor="pointer"
        onClick={(e) => {
          e.stopPropagation();
          toggleExpand();
        }}
      >
        <Flex alignItems="center" flex="1">
          <IconButton
            aria-label={isStarred ? 'Remove from favorites' : 'Add to favorites'}
            icon={<StarIcon color={isStarred ? starredColor : "gray.300"} />}
            size="sm"
            variant="ghost"
            mr={2}
            onClick={(e) => { e.stopPropagation(); onStar(); }}
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
              mr={2}
            />
          )}
          
          {onEqualizer && (
            <IconButton
              aria-label="Equalizer"
              title="Open Equalizer"
              icon={<Box as={FaSlidersH} transform="rotate(90deg)" />}
              onClick={(e) => { e.stopPropagation(); onEqualizer(); }}
              variant="ghost"
              size="sm"
              mr={2}
            />
          )}
          
          {onEdit && (
            <IconButton
              aria-label="Edit"
              title="Edit"
              icon={<SettingsIcon />}
              onClick={(e) => {
                e.stopPropagation();
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
              mr={2}
            />
          )}

          <Box onClick={handleItemClick}>
            <Text fontWeight="bold" mr={3} color={headingColor}>
              {item.name}
            </Text>
          </Box>
          
          <HStack spacing={1} mr={3}>
            {getStatusBadges()}
            <Badge
              colorScheme={item.enabled ? "green" : "red"}
              px={2}
              py={1}
              borderRadius="full"
              cursor="pointer"
              userSelect="none"
              _hover={{ opacity: 0.8 }}
              onClick={(e) => { e.stopPropagation(); onActivate(); }}
              title={item.enabled ? 'Click to disable' : 'Click to enable'}
            >
              {item.enabled ? 'Enabled' : 'Disabled'}
            </Badge>
            
            {/* Listen badge for sources */}
            {isSource(item) && (
              <Badge
                colorScheme="teal"
                mr={1}
                borderRadius="full"
                px={2}
                py={1}
                userSelect="none"
                cursor="pointer"
                _hover={{ opacity: 0.8 }}
                onClick={(e) => {
                  e.stopPropagation();
                  window.open(`/site/listen/source/${encodeURIComponent(item.name)}`, '_blank');
                }}
                title="Click to open listen page for this source"
                display="inline-flex"
                alignItems="center"
                gap={1}
              >
                <Box as={FaHeadphones} fontSize="xs" />
                Listen
              </Badge>
            )}
            
            {/* Listen badge for routes */}
            {isRoute(item) && (
              <Badge
                colorScheme="orange"
                mr={1}
                borderRadius="full"
                px={2}
                py={1}
                userSelect="none"
                cursor="pointer"
                _hover={{ opacity: 0.8 }}
                onClick={(e) => {
                  e.stopPropagation();
                  window.open(`/site/listen/route/${encodeURIComponent(item.name)}`, '_blank');
                }}
                title="Click to open listen page for this route"
                display="inline-flex"
                alignItems="center"
                gap={1}
              >
                <Box as={FaHeadphones} fontSize="xs" />
                Listen
              </Badge>
            )}
            
            {/* Listen badge for sinks */}
            {isSink(item) && onListen && (
              <Badge
                colorScheme={isActive ? "purple" : "blue"}
                mr={1}
                borderRadius="full"
                px={2}
                py={1}
                userSelect="none"
                cursor="pointer"
                _hover={{ opacity: 0.8 }}
                onClick={(e) => {
                  e.stopPropagation();
                  window.open(`/site/listen/sink/${encodeURIComponent(item.name)}`, '_blank');
                }}
                title="Click to open listen page for this sink"
                display="inline-flex"
                alignItems="center"
                gap={1}
              >
                <Box as={FaHeadphones} fontSize="xs" />
                {isActive ? "Listening" : "Listen"}
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
                onClick={(e) => { e.stopPropagation(); onVnc(); }}
                title="Open VNC"
              >
                VNC
              </Badge>
            )}
          </HStack>
        </Flex>
        
        <Flex>
          <IconButton
            aria-label={expanded ? "Collapse" : "Expand"}
            icon={expanded ? <ChevronUpIcon /> : <ChevronDownIcon />}
            size="sm"
            variant="ghost"
          />
        </Flex>
      </Flex>
      
      <Collapse in={expanded} animateOpacity style={{ width: '100%' }}>
        <Box p={4} width="100%">
          {/* Volume control - always visible */}
          {(isSource(item) || isSink(item) || isRoute(item)) && (
            <Box mt={3} mb={3}>
              <Flex mb={1} align="center">
                <Box as="i" className="fas fa-volume-up" mr={2} />
                <Text fontWeight="medium">Volume:</Text>
              </Flex>
              <HStack spacing={3} ml={2}>
                <VolumeSlider
                  value={isRoute(item) ? (item.volume || 100) : (item.volume)}
                  onChange={(value) => onUpdateVolume && onUpdateVolume(value)}
                />
                {isSource(item) && item.vnc_ip && (
                <>
                  {/* Media controls for VNC sources */}
                  {onControlSource && (
                      <>
                        <IconButton
                          aria-label="Previous Track"
                          icon={<FaStepBackward />}
                          size="sm"
                          colorScheme="purple"
                          variant="outline"
                          onClick={(e) => { e.stopPropagation(); onControlSource('prevtrack'); }}
                          ml={5}
                          mt={-5}
                        />
                        <IconButton
                          aria-label="Play/Pause"
                          icon={<FaPlay />}
                          size="sm"
                          colorScheme="purple"
                          variant="outline"
                          onClick={(e) => { e.stopPropagation(); onControlSource('play'); }}
                          mt={-5}
                        />
                        <IconButton
                          aria-label="Next Track"
                          icon={<FaStepForward />}
                          size="sm"
                          colorScheme="purple"
                          variant="outline"
                          onClick={(e) => { e.stopPropagation(); onControlSource('nexttrack'); }}
                          mt={-5}
                        />
                      </>
                  )}
                </>
              )}
              </HStack>
            </Box>
          )}
          
          <Flex justify="flex-start" wrap="wrap" width="100%" justifyContent="center" gap={2} mb={3}>
            
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
          
          {isSink(item) && (
            <VStack spacing={4} align="stretch" width="100%">
              
              {/* Technical details */}
              {(item.ip || item.port || item.sample_rate || item.bit_depth) && (
                <Box>
                  <Text fontWeight="semibold" mb={2}>Technical Details</Text>
                  <SimpleGrid columns={{ base: 1, md: 2, lg: 3, "2xl": 4 }} spacing={2}>
                    {item.ip && <Text fontSize="sm"><i className="fas fa-network-wired"></i> IP: {item.ip}</Text>}
                    {item.port && <Text fontSize="sm"><i className="fas fa-plug"></i> Port: {item.port}</Text>}
                    {item.sample_rate && <Text fontSize="sm"><i className="fas fa-wave-square"></i> Sample Rate: {item.sample_rate}Hz</Text>}
                    {item.bit_depth && <Text fontSize="sm"><i className="fas fa-microchip"></i> Bit Depth: {item.bit_depth}-bit</Text>}
                  </SimpleGrid>
                </Box>
              )}
              
              {/* Parent Groups */}
              {!item.is_group && parentGroups.length > 0 && (
                <Box>
                  <Text fontWeight="semibold" mb={2}>Member of Groups</Text>
                  <VStack align="stretch" spacing={1} width="100%">
                    {parentGroups.map((group, index) => (
                      <Box
                        key={index}
                        p={2}
                        borderWidth="1px"
                        borderRadius="md"
                        cursor={navigate ? "pointer" : "default"}
                        _hover={navigate ? { bg: useColorModeValue("gray.100", "gray.700") } : {}}
                        onClick={(e) => {
                          e.stopPropagation();
                          if (navigate) {
                            navigate(group.type === 'sources' ? 'sources' : 'sinks', group.name);
                          }
                        }}
                        bg={useColorModeValue(group.type === 'sources' ? "green.50" : "blue.50",
                                             group.type === 'sources' ? "green.900" : "blue.900")}
                        borderColor={useColorModeValue(group.type === 'sources' ? "green.200" : "blue.200",
                                                      group.type === 'sources' ? "green.700" : "blue.700")}
                      >
                        <Text fontSize="sm">
                          {group.name}
                          {navigate && (
                            <Box as="span" ml={1} color={useColorModeValue("blue.500", "blue.300")}>
                              <i className="fas fa-external-link-alt" style={{ fontSize: '0.7em' }}></i>
                            </Box>
                          )}
                        </Text>
                      </Box>
                    ))}
                  </VStack>
                </Box>
              )}
              
            </VStack>
          )}
        </Box>
      </Collapse>
    </Box>
  );
};

export default ResourceListItem;
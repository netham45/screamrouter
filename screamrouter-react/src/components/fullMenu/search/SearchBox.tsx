import React, { useState, useRef } from 'react';
import {
  Box,
  Input,
  InputGroup,
  InputLeftElement,
  Popover,
  PopoverTrigger,
  PopoverContent,
  PopoverBody,
  VStack,
  Text,
  Badge,
  Flex,
  useColorModeValue,
  Divider,
  HStack,
  Portal,
  useOutsideClick
} from '@chakra-ui/react';
import { SearchIcon } from '@chakra-ui/icons';
import { Source, Sink, Route } from '../../../api/api';

interface SearchResult {
  id: string;
  name: string;
  type: 'sources' | 'sinks' | 'routes' | 'group-source' | 'group-sink';
  enabled: boolean;
  volume?: number;
  ip?: string;
  isGroup?: boolean;
  matchedMember?: string; // If this is a group that matched because of a member
}

interface SearchBoxProps {
  sources: Source[];
  sinks: Sink[];
  routes: Route[];
  navigate: (type: 'sources' | 'sinks' | 'routes' | 'group-source' | 'group-sink', itemName: string) => void;
}

const SearchBox: React.FC<SearchBoxProps> = ({ sources, sinks, routes, navigate }) => {
  const [searchTerm, setSearchTerm] = useState('');
  const [isOpen, setIsOpen] = useState(false);
  const [results, setResults] = useState<SearchResult[]>([]);
  const inputRef = useRef<HTMLInputElement>(null);
  const popoverRef = useRef<HTMLDivElement>(null);

  // Colors
  const popoverBg = useColorModeValue('white', 'gray.800');
  const hoverBg = useColorModeValue('gray.100', 'gray.700');
  const textColor = useColorModeValue('gray.800', 'gray.200');
  const mutedColor = useColorModeValue('gray.600', 'gray.400');
  const placeholderColor = useColorModeValue('gray.200', 'gray.400');

  // Close popover when clicking outside
  useOutsideClick({
    ref: popoverRef,
    handler: () => setIsOpen(false),
  });

  // Search function
  const performSearch = (term: string) => {
    if (!term.trim()) {
      setResults([]);
      return;
    }

    const lowerTerm = term.toLowerCase();
    const matchedResults: SearchResult[] = [];

    // Track which items have already been added to avoid duplicates
    const addedItems = new Set<string>();

    // Search sources
    sources.forEach(source => {
      const nameMatch = source.name.toLowerCase().includes(lowerTerm);
      const ipMatch = source.ip?.toLowerCase().includes(lowerTerm);
      
      if (nameMatch || ipMatch) {
        matchedResults.push({
          id: `source-${source.name}`,
          name: source.name,
          type: source.is_group ? 'group-source' : 'sources',
          enabled: source.enabled,
          volume: source.volume,
          ip: source.ip,
          isGroup: source.is_group
        });
        
        // Mark this source as added
        addedItems.add(`source-${source.name}`);
      }

      // If this is a group and any of its members match the search term, add the group
      if (source.is_group && source.group_members && !addedItems.has(`source-${source.name}`)) {
        const matchingMember = source.group_members.find(member =>
          member.toLowerCase().includes(lowerTerm)
        );
        
        if (matchingMember) {
          matchedResults.push({
            id: `source-group-${source.name}`,
            name: source.name,
            type: 'group-source',
            enabled: source.enabled,
            volume: source.volume,
            ip: source.ip,
            isGroup: true,
            matchedMember: matchingMember
          });
          
          // Mark this group as added
          addedItems.add(`source-${source.name}`);
        }
      }
    });

    // Search sinks
    sinks.forEach(sink => {
      const nameMatch = sink.name.toLowerCase().includes(lowerTerm);
      const ipMatch = sink.ip?.toLowerCase().includes(lowerTerm);
      
      if (nameMatch || ipMatch) {
        matchedResults.push({
          id: `sink-${sink.name}`,
          name: sink.name,
          type: sink.is_group ? 'group-sink' : 'sinks',
          enabled: sink.enabled,
          volume: sink.volume,
          ip: sink.ip,
          isGroup: sink.is_group
        });
        
        // Mark this sink as added
        addedItems.add(`sink-${sink.name}`);
      }

      // If this is a group and any of its members match the search term, add the group
      if (sink.is_group && sink.group_members && !addedItems.has(`sink-${sink.name}`)) {
        const matchingMember = sink.group_members.find(member =>
          member.toLowerCase().includes(lowerTerm)
        );
        
        if (matchingMember) {
          matchedResults.push({
            id: `sink-group-${sink.name}`,
            name: sink.name,
            type: 'group-sink',
            enabled: sink.enabled,
            volume: sink.volume,
            ip: sink.ip,
            isGroup: true,
            matchedMember: matchingMember
          });
          
          // Mark this group as added
          addedItems.add(`sink-${sink.name}`);
        }
      }
    });

    // Search routes
    routes.forEach(route => {
      const nameMatch = route.name.toLowerCase().includes(lowerTerm);
      const sourceMatch = route.source.toLowerCase().includes(lowerTerm);
      const sinkMatch = route.sink.toLowerCase().includes(lowerTerm);
      
      if (nameMatch || sourceMatch || sinkMatch) {
        matchedResults.push({
          id: `route-${route.name}`,
          name: route.name,
          type: 'routes',
          enabled: route.enabled,
          volume: route.volume
        });
      }
    });

    setResults(matchedResults);
  };

  // Handle input change
  const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = e.target.value;
    setSearchTerm(value);
    performSearch(value);
    setIsOpen(true);
  };

  // Handle result click
  const handleResultClick = (result: SearchResult) => {
    navigate(result.type, result.name);
    setIsOpen(false);
    setSearchTerm('');
    setResults([]);
    if (inputRef.current) {
      inputRef.current.blur();
    }
  };

  // Get badge color based on type
  const getBadgeColor = (type: string) => {
    if (type === 'sources') return 'green';
    if (type === 'sinks') return 'blue';
    if (type === 'routes') return 'red';
    if (type === 'group-source') return 'purple';
    if (type === 'group-sink') return 'purple';
    return 'gray';
  };

  // Get badge text based on type
  const getBadgeText = (type: string) => {
    if (type === 'sources') return 'Source';
    if (type === 'sinks') return 'Sink';
    if (type === 'routes') return 'Route';
    if (type === 'group-source') return 'Source Group';
    if (type === 'group-sink') return 'Sink Group';
    return type;
  };

  // Format volume as percentage
  const formatVolume = (volume?: number) => {
    if (volume === undefined) return '100%';
    return `${Math.round(volume)}%`;
  };

  return (
    <Box width="100%" maxW="400px" position="relative" ref={popoverRef}>
      <Popover
        isOpen={isOpen && results.length > 0}
        autoFocus={false}
        placement="bottom-start"
        closeOnBlur={false}
        isLazy
      >
        <PopoverTrigger>
          <InputGroup>
            <InputLeftElement pointerEvents="none">
              <SearchIcon color={placeholderColor} />
            </InputLeftElement>
            <Input
              ref={inputRef}
              placeholder="Search sources, sinks, routes..."
              value={searchTerm}
              onChange={handleInputChange}
              onFocus={() => {
                if (searchTerm && results.length > 0) {
                  setIsOpen(true);
                }
              }}
              borderRadius="md"
              _placeholder={{
                color: 'gray.300'
              }}
              _focus={{ borderColor: 'blue.400', boxShadow: '0 0 0 1px var(--chakra-colors-blue-400)' }}
            />
          </InputGroup>
        </PopoverTrigger>
        <Portal>
          <PopoverContent
            width="400px"
            maxH="400px"
            overflowY="auto"
            bg={popoverBg}
            borderColor="gray.200"
            boxShadow="lg"
            _dark={{ borderColor: 'gray.600' }}
          >
            <PopoverBody p={0}>
              <VStack spacing={0} align="stretch" divider={<Divider />}>
                {results.map((result) => (
                  <Box
                    key={result.id}
                    p={3}
                    cursor="pointer"
                    _hover={{ bg: hoverBg }}
                    onClick={() => handleResultClick(result)}
                  >
                    <Flex justifyContent="space-between" alignItems="center" mb={1}>
                      <HStack>
                        <Badge colorScheme={getBadgeColor(result.type)}>
                          {getBadgeText(result.type)}
                        </Badge>
                        <Badge colorScheme={result.enabled ? 'green' : 'red'}>
                          {result.enabled ? 'Enabled' : 'Disabled'}
                        </Badge>
                      </HStack>
                      {result.volume !== undefined && (
                        <Text fontSize="sm" color={mutedColor}>
                          Vol: {formatVolume(result.volume)}
                        </Text>
                      )}
                    </Flex>
                    <Text fontWeight="bold" color={textColor}>
                      {result.name}
                    </Text>
                    {result.ip && (
                      <Text fontSize="sm" color={mutedColor}>
                        IP: {result.ip}
                      </Text>
                    )}
                    {result.matchedMember && (
                      <Flex alignItems="center" mt={1}>
                        <Badge colorScheme="orange" size="sm" mr={1}>Member match</Badge>
                        <Text fontSize="sm" color={mutedColor}>
                          Contains: <Text as="span" fontWeight="bold">{result.matchedMember}</Text>
                        </Text>
                      </Flex>
                    )}
                  </Box>
                ))}
              </VStack>
            </PopoverBody>
          </PopoverContent>
        </Portal>
      </Popover>
    </Box>
  );
};

export default SearchBox;
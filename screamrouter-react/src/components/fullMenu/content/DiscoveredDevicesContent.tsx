import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Alert,
  AlertIcon,
  Badge,
  Box,
  Button,
  Flex,
  Heading,
  HStack,
  Select,
  Switch,
  Spinner,
  Stack,
  Tag,
  Text,
  Wrap,
  WrapItem,
  useColorModeValue,
} from '@chakra-ui/react';
import ApiService from '../../../api/api';
import { ContentProps } from '../types';
import { DiscoveredDevice } from '../../../types/preferences';
import { buildDeviceKey, formatLastSeen, getMethodColor, isDeviceRecentlySeen } from '../../../utils/discovery';
import { formatProcessTag } from '../../../utils/processTags';
import { openInNewWindow } from '../../fullMenu/utils';

type DiscoveryFilter = 'all' | 'sources' | 'sinks';

const roleMatchesFilter = (device: DiscoveredDevice, filter: DiscoveryFilter): boolean => {
  if (filter === 'all') return true;
  const role = (device.role || '').toLowerCase();
  if (filter === 'sinks') {
    return role === 'sink';
  }
  return role !== 'sink';
};

const describeDevice = (device: DiscoveredDevice) => {
  const role = (device.role || '').toLowerCase();
  if (role === 'sink') {
    return 'sink';
  }
  return 'source';
};

const formatPropertyValue = (value: unknown): string => {
  if (value === null || value === undefined) {
    return '';
  }
  if (typeof value === 'object') {
    try {
      return JSON.stringify(value);
    } catch (error) {
      return '[object]';
    }
  }
  return String(value);
};

const DiscoveredDevicesContent: React.FC<ContentProps> = ({ setCurrentCategory: _setCurrentCategory }) => {
  const [devices, setDevices] = useState<DiscoveredDevice[]>([]);
  const [counts, setCounts] = useState({
    total: 0,
    sources: 0,
    sinks: 0,
    unmatched_total: 0,
    unmatched_sources: 0,
    unmatched_sinks: 0,
  });
  const [filter, setFilter] = useState<DiscoveryFilter>('all');
  const [showMatched, setShowMatched] = useState(true);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);

  const mutedText = useColorModeValue('gray.600', 'gray.300');
  const subtleText = useColorModeValue('gray.500', 'gray.400');
  const surfaceColor = useColorModeValue('gray.50', 'gray.700');
  const surfaceHover = useColorModeValue('gray.100', 'gray.600');
  const borderColor = useColorModeValue('gray.200', 'whiteAlpha.200');

  const loadDevices = useCallback(async () => {
    setIsLoading(true);
    setError(null);
    try {
      const response = await ApiService.getUnmatchedDiscoveredDevices();
      setDevices(response.data?.devices ?? []);
      const incomingCounts = response.data?.counts ?? {};
      setCounts({
        total: incomingCounts.total ?? 0,
        sources: incomingCounts.sources ?? 0,
        sinks: incomingCounts.sinks ?? 0,
        unmatched_total: incomingCounts.unmatched_total ?? incomingCounts.total ?? 0,
        unmatched_sources: incomingCounts.unmatched_sources ?? 0,
        unmatched_sinks: incomingCounts.unmatched_sinks ?? 0,
      });
      setLastUpdated(new Date());
    } catch (err) {
      console.error('Failed to load discovered devices', err);
      setError('Unable to load discovered devices. Please try again.');
    } finally {
      setIsLoading(false);
    }
  }, []);

  useEffect(() => {
    void loadDevices();
  }, [loadDevices]);

  const filteredDevices = useMemo(
    () => devices
      .filter(device => roleMatchesFilter(device, filter))
      .filter(device => (showMatched ? true : !device.matched)),
    [devices, filter, showMatched]
  );

  const openAddPageForDevice = useCallback((device: DiscoveredDevice) => {
    const role = describeDevice(device);
    const params = new URLSearchParams();
    const preferredName = device.name || device.tag || device.ip;
    if (preferredName) {
      params.set('prefill_name', preferredName);
    }
    if (device.ip) {
      params.set('prefill_ip', device.ip);
    }
    if (device.port) {
      params.set('prefill_port', String(device.port));
    }

    const protocolHint = (device.properties?.protocol || device.properties?.mode || device.device_type || device.discovery_method || '').toString().toLowerCase();
    if (role === 'sink') {
      if (protocolHint.includes('rtp')) {
        params.set('prefill_protocol', 'rtp');
      } else if (protocolHint.includes('webrtc')) {
        params.set('prefill_protocol', 'webrtc');
      } else if (protocolHint.includes('timeshift')) {
        params.set('prefill_protocol', 'timeshift');
      } else {
        params.set('prefill_protocol', 'scream');
      }
      openInNewWindow(`/site/add-sink?${params.toString()}`, 800, 700);
    } else {
      openInNewWindow(`/site/add-source?${params.toString()}`, 800, 700);
    }
  }, []);

  return (
    <Box p={4}>
      <Flex justify="space-between" align="center" mb={4} gap={3} flexWrap="wrap">
        <Box>
          <Heading size="md">Discovered Devices</Heading>
          <Text fontSize="sm" color={mutedText}>
            {showMatched
              ? `Showing all discovered devices (${counts.total} total: ${counts.sources} sources, ${counts.sinks} sinks).`
              : `Showing unmatched devices (${counts.unmatched_total} of ${counts.total}; ${counts.unmatched_sources} sources, ${counts.unmatched_sinks} sinks).`}
            {lastUpdated && ` Updated ${lastUpdated.toLocaleTimeString()}.`}
          </Text>
        </Box>
        <HStack spacing={3}>
          <Select
            size="sm"
            value={filter}
            onChange={event => setFilter(event.target.value as DiscoveryFilter)}
            width="150px"
          >
            <option value="all">All</option>
            <option value="sources">Sources</option>
            <option value="sinks">Sinks</option>
          </Select>
          <HStack spacing={2}>
            <Switch
              size="sm"
              isChecked={showMatched}
              onChange={event => setShowMatched(event.target.checked)}
            />
            <Text fontSize="sm" color={mutedText}>Show matched</Text>
          </HStack>
          <Button
            size="sm"
            onClick={() => void loadDevices()}
            leftIcon={<i className="fas fa-sync" />}
            isLoading={isLoading}
            loadingText="Refreshing"
          >
            Refresh
          </Button>
        </HStack>
      </Flex>

      {error && (
        <Alert status="error" borderRadius="md" mb={4}>
          <AlertIcon />
          {error}
        </Alert>
      )}

      {isLoading && (
        <Flex justify="center" py={10}>
          <Spinner size="lg" />
        </Flex>
      )}

      {!isLoading && filteredDevices.length === 0 && (
        <Box
          borderWidth="1px"
          borderRadius="md"
          borderColor={borderColor}
          p={6}
          textAlign="center"
        >
          <Text fontWeight="semibold">All clear</Text>
          <Text fontSize="sm" color={mutedText} mt={2}>
            No unmatched devices are waiting to be added. New traffic will appear here automatically.
          </Text>
        </Box>
      )}

      <Stack spacing={3} mt={isLoading ? 0 : 2}>
        {filteredDevices.map(device => {
          const deviceKey = buildDeviceKey(device);
          const methodColor = getMethodColor(device.discovery_method);
          const role = describeDevice(device);
          const recentlySeen = isDeviceRecentlySeen(device);

          return (
            <Box
              key={deviceKey}
              borderWidth="1px"
              borderRadius="lg"
              borderColor={borderColor}
              bg={surfaceColor}
              _hover={{ bg: surfaceHover, borderColor: methodColor }}
              transition="all 0.2s ease"
              p={3}
            >
              <Flex justify="space-between" align={{ base: 'flex-start', md: 'center' }} gap={3} wrap="wrap">
                <Box flex="1" minW="0">
                  <Flex align="center" gap={2} wrap="wrap">
                    <Text fontWeight="semibold" fontSize="sm" noOfLines={3}>
                      {device.name || device.ip || 'Unknown device'}
                    </Text>
                    {device.role && (
                      <Badge colorScheme="blue" variant="subtle" fontSize="0.65rem">
                        {role}
                      </Badge>
                    )}
                    {device.device_type && (
                      <Badge colorScheme="cyan" variant="subtle" fontSize="0.65rem">
                        {device.device_type}
                      </Badge>
                    )}
                    <Badge colorScheme={methodColor} variant="outline" fontSize="0.65rem">
                      {device.discovery_method}
                    </Badge>
                    {!recentlySeen && (
                      <Badge colorScheme="orange" variant="subtle" fontSize="0.65rem">
                        stale
                      </Badge>
                    )}
                    {device.matched && (
                      <Badge colorScheme="gray" variant="subtle" fontSize="0.65rem">
                        matched
                      </Badge>
                    )}
                  </Flex>
                  <Flex mt={1} gap={2} align="center" color={mutedText} fontSize="xs" wrap="wrap">
                    <Text>{device.ip}</Text>
                    {device.port && (
                      <>
                        <Text aria-hidden="true">•</Text>
                        <Text>Port {device.port}</Text>
                      </>
                    )}
                    {device.tag && (
                      <>
                        <Text aria-hidden="true">•</Text>
                        <Text>
                          Tag {device.role === 'process' ? formatProcessTag(device.tag) : device.tag}
                        </Text>
                      </>
                    )}
                  </Flex>
                </Box>
                <Flex align="center" gap={3}>
                  <Text fontSize="xs" color={subtleText} whiteSpace="nowrap">
                    {formatLastSeen(device.last_seen)}
                  </Text>
                  {device.matched && (
                    <Text fontSize="xs" color={subtleText} whiteSpace="nowrap">
                      Matched ({device.match_reason || 'existing config'})
                    </Text>
                  )}
                  <Button
                    size="sm"
                    colorScheme={role === 'sink' ? 'purple' : 'green'}
                    variant="solid"
                    onClick={() => openAddPageForDevice(device)}
                  >
                    Add {role}
                  </Button>
                </Flex>
              </Flex>

              {device.properties && Object.keys(device.properties).length > 0 && (
                <Wrap mt={3} spacing={1.5} shouldWrapChildren>
                  {Object.entries(device.properties)
                    .filter(([key]) => key !== 'identifier')
                    .map(([key, value]) => (
                      <WrapItem key={key}>
                        <Tag size="sm" variant="subtle" colorScheme="gray">
                          {key}: {formatPropertyValue(value)}
                        </Tag>
                      </WrapItem>
                    ))}
                </Wrap>
              )}
            </Box>
          );
        })}
      </Stack>
    </Box>
  );
};

export default DiscoveredDevicesContent;

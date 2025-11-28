import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Alert,
  AlertIcon,
  Badge,
  Box,
  Button,
  Flex,
  Modal,
  ModalBody,
  ModalCloseButton,
  ModalContent,
  ModalFooter,
  ModalHeader,
  ModalOverlay,
  Spinner,
  Stack,
  Tag,
  Text,
  Wrap,
  WrapItem,
  useColorModeValue,
} from '@chakra-ui/react';
import { DiscoveredDevice } from '../../types/preferences';
import { formatProcessTag } from '../../utils/processTags';
import { buildDeviceKey, formatLastSeen, getMethodColor, isDeviceRecentlySeen } from '../../utils/discovery';

interface MdnsDiscoveryModalProps {
  isOpen: boolean;
  isLoading: boolean;
  devices: DiscoveredDevice[];
  error: string | null;
  filter: 'all' | 'sources' | 'sinks';
  onClose: () => void;
  onRefresh: () => void;
  onSelect: (device: DiscoveredDevice) => void;
}

const toLowerToken = (value: unknown): string | null => {
  if (typeof value !== 'string') {
    return null;
  }
  const trimmed = value.trim();
  return trimmed ? trimmed.toLowerCase() : null;
};

const roleTokens = (device: DiscoveredDevice) => {
  const rawValues: unknown[] = [
    device.role,
    device.device_type,
    device.properties['mode'],
    device.properties['type'],
    device.properties['role'],
  ];

  return rawValues
    .map(toLowerToken)
    .filter((value): value is string => Boolean(value));
};

const typeMatchesFilter = (device: DiscoveredDevice, filter: 'all' | 'sources' | 'sinks') => {
  if (filter === 'all') return true;

  const normalizedRole = toLowerToken(device.role) || '';
  if (filter === 'sources' && normalizedRole.includes('sink')) {
    return false;
  }
  if (filter === 'sinks' && normalizedRole.includes('source')) {
    return false;
  }
  if (filter === 'sources' && normalizedRole.includes('source')) {
    return true;
  }
  if (filter === 'sinks' && normalizedRole.includes('sink')) {
    return true;
  }

  const tokens = roleTokens(device);
  if (tokens.length === 0) {
    // Without metadata, surface device for manual selection
    return true;
  }

  const roleText = tokens.join(' ');
  if (filter === 'sources') {
    return roleText.includes('source') || roleText.includes('sender') || roleText.includes('transmit');
  }
  return (
    roleText.includes('sink') ||
    roleText.includes('receiver') ||
    roleText.includes('speaker') ||
    roleText.includes('output')
  );
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

const MdnsDiscoveryModal: React.FC<MdnsDiscoveryModalProps> = ({
  isOpen,
  isLoading,
  devices,
  error,
  filter,
  onClose,
  onRefresh,
  onSelect,
}) => {
  const [actionError, setActionError] = useState<string | null>(null);
  const [pendingKey, setPendingKey] = useState<string | null>(null);

  useEffect(() => {
    if (!isOpen) {
      setActionError(null);
      setPendingKey(null);
    }
  }, [isOpen]);

  const filteredDevices = useMemo(
    () => devices
      .filter(device => isDeviceRecentlySeen(device))
      .filter(device => typeMatchesFilter(device, filter)),
    [devices, filter]
  );

  const handleDeviceClick = useCallback(async (device: DiscoveredDevice) => {
    setActionError(null);
    const deviceKey = buildDeviceKey(device);
    setPendingKey(deviceKey);

    try {
      const role = (device.role || '').toLowerCase();
      onSelect(device);
    } catch (apiError) {
      console.error('Unable to add discovered device', apiError);
      setActionError('Unable to add the selected device. Please try again.');
    } finally {
      setPendingKey(null);
    }
  }, [onSelect]);

  const badgeColor = 'blue';
  const surfaceColor = useColorModeValue('gray.50', 'gray.700');
  const surfaceHover = useColorModeValue('gray.100', 'gray.600');
  const borderColor = useColorModeValue('gray.200', 'whiteAlpha.200');
  const mutedText = useColorModeValue('gray.600', 'gray.300');
  const subtleText = useColorModeValue('gray.500', 'gray.400');

  return (
    <Modal isOpen={isOpen} onClose={onClose} size="xl" isCentered>
      <ModalOverlay />
      <ModalContent>
        <ModalHeader>Discover Devices</ModalHeader>
        <ModalCloseButton />
        <ModalBody>
          <Flex align="center" justify="space-between" mb={4} gap={3} flexWrap="wrap">
            <Text fontSize="sm" color={subtleText}>
              Showing {filter === 'all' ? 'all devices' : filter === 'sources' ? 'source devices' : 'sink devices'}
            </Text>
            <Button
              size="sm"
              onClick={onRefresh}
              leftIcon={<i className="fas fa-sync" />}
              isLoading={isLoading}
              loadingText="Scanning"
            >
              Refresh
            </Button>
          </Flex>

          {error && (
            <Alert status="error" borderRadius="md" mb={4}>
              <AlertIcon />
              {error}
            </Alert>
          )}

          {actionError && (
            <Alert status="error" borderRadius="md" mb={4}>
              <AlertIcon />
              {actionError}
            </Alert>
          )}

          {isLoading && (
            <Flex justify="center" py={8}>
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
              <Text fontSize="sm" color={mutedText}>
                No devices discovered yet. Ensure the device is powered on and broadcasting mDNS.
              </Text>
            </Box>
          )}

          <Stack spacing={2} mt={isLoading ? 0 : 2}>
            {filteredDevices.map(device => {
              const deviceKey = buildDeviceKey(device);
              const isProcessing = pendingKey === deviceKey;
              const methodColor = getMethodColor(device.discovery_method);
              const getStringProperty = (...keys: string[]): string => {
                for (const key of keys) {
                  const raw = device.properties?.[key];
                  if (raw === undefined || raw === null) {
                    continue;
                  }
                  const value = Array.isArray(raw) ? raw.join(', ') : String(raw);
                  const normalized = value.trim();
                  if (normalized) {
                    return normalized;
                  }
                }
                return '';
              };
              const sapSessionName = getStringProperty(
                'sap_session_name',
                'sap_session',
                'sap_name',
                'session_name',
                'sdp_session_name',
                'sdp_name',
                'session'
              );

              return (
                <Box
                  key={deviceKey}
                  borderWidth="1px"
                  borderRadius="lg"
                  borderColor={borderColor}
                  bg={surfaceColor}
                  _hover={isProcessing ? {} : { bg: surfaceHover, borderColor: badgeColor, cursor: 'pointer' }}
                  transition="all 0.2s ease"
                  p={3}
                  opacity={isProcessing ? 0.75 : 1}
                  cursor={isProcessing ? 'progress' : 'pointer'}
                  onClick={() => {
                    if (!isProcessing) {
                      void handleDeviceClick(device);
                    }
                  }}
                >
                  <Flex
                    justify="space-between"
                    align={{ base: 'flex-start', md: 'center' }}
                    gap={3}
                    wrap="wrap"
                  >
                    <Box flex="1" minW="0">
                      <Flex align="center" gap={2} wrap="wrap">
                        <Text fontWeight="semibold" fontSize="sm" noOfLines={3}>
                          {device.name || device.ip || 'Unnamed device'}
                        </Text>
                        {device.role && (
                          <Badge colorScheme={badgeColor} variant="subtle" fontSize="0.65rem">
                            {device.role}
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
                        {sapSessionName && (
                          <>
                            <Text aria-hidden="true">•</Text>
                            <Text>SAP: {sapSessionName}</Text>
                          </>
                        )}
                      </Flex>
                    </Box>
                    <Flex align="center" gap={2}>
                      <Text fontSize="xs" color={subtleText} whiteSpace="nowrap">
                        {formatLastSeen(device.last_seen)}
                      </Text>
                      {isProcessing && <Spinner size="sm" />}
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
        </ModalBody>
        <ModalFooter>
          <Button variant="ghost" onClick={onClose}>
            Close
          </Button>
        </ModalFooter>
      </ModalContent>
    </Modal>
  );
};

export default MdnsDiscoveryModal;

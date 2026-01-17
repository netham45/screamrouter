import React, { useMemo } from 'react';
import {
  Box,
  Heading,
  Text,
  Stack,
  HStack,
  Badge,
  Icon,
  useColorModeValue,
  Table,
  Thead,
  Tbody,
  Tr,
  Th,
  Td,
  TableContainer,
  Tooltip,
  Divider
} from '@chakra-ui/react';
import { FaMicrophone, FaVolumeUp, FaPlug, FaCheckCircle, FaExclamationTriangle } from 'react-icons/fa';
import { ContentProps } from '../types';
import { SystemAudioDeviceInfo } from '../../api/api';

const formatChannelList = (channels: number[]): string => {
  if (!channels || channels.length === 0) {
    return '—';
  }
  return channels.join(', ');
};

const formatSampleRate = (rate: number): string => {
  if (rate >= 1000) {
    const kilohertz = rate / 1000;
    return Number.isInteger(kilohertz) ? `${kilohertz} kHz` : `${kilohertz.toFixed(1)} kHz`;
  }
  return `${rate} Hz`;
};

const renderSampleRates = (rates: number[]): string => {
  if (!rates || rates.length === 0) {
    return '—';
  }
  return rates
    .slice()
    .sort((a, b) => a - b)
    .map(formatSampleRate)
    .join(', ');
};

const formatBitDepths = (device: SystemAudioDeviceInfo): string => {
  const list = Array.isArray(device.bit_depths)
    ? device.bit_depths.filter((value): value is number => typeof value === 'number' && Number.isFinite(value) && value > 0)
    : [];
  if (list.length === 0 && typeof device.bit_depth === 'number' && device.bit_depth > 0) {
    list.push(device.bit_depth);
  }
  if (list.length === 0) {
    return '—';
  }
  const unique = Array.from(new Set(list));
  unique.sort((a, b) => a - b);
  return unique.map(depth => `${depth}-bit`).join(', ');
};

interface DeviceSectionProps {
  title: string;
  icon: React.ComponentType;
  devices: SystemAudioDeviceInfo[];
}

const DeviceSection: React.FC<DeviceSectionProps> = ({ title, icon, devices }) => {
  const sectionBg = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');

  const offlineCount = useMemo(() => devices.filter(device => !device.present).length, [devices]);

  return (
    <Box
      p={6}
      borderWidth="1px"
      borderRadius="md"
      bg={sectionBg}
      borderColor={borderColor}
      boxShadow="sm"
    >
      <HStack spacing={3} mb={4} align="center">
        <Icon as={icon} boxSize={5} color={useColorModeValue('blue.500', 'blue.300')} />
        <Heading as="h3" size="md">
          {title}
        </Heading>
        <Badge colorScheme="purple" borderRadius="full" px={3} py={1}>
          {devices.length} device{devices.length === 1 ? '' : 's'}
        </Badge>
        {offlineCount > 0 && (
          <Tooltip label={`${offlineCount} device${offlineCount === 1 ? '' : 's'} currently offline`}>
            <HStack spacing={1} color="orange.400">
              <Icon as={FaExclamationTriangle} />
              <Text fontSize="sm">{offlineCount} offline</Text>
            </HStack>
          </Tooltip>
        )}
      </HStack>
      {devices.length === 0 ? (
        <Text color={useColorModeValue('gray.600', 'gray.400')}>
          No devices discovered yet. Connect a system audio device to see it listed here.
        </Text>
      ) : (
        <TableContainer>
          <Table variant="simple" size="sm">
            <Thead>
              <Tr>
                <Th>Tag</Th>
                <Th>Friendly Name</Th>
                <Th>Card/Device</Th>
                <Th>Channels</Th>
                <Th>Sample Rates</Th>
                <Th>Bit Depths</Th>
                <Th>Status</Th>
              </Tr>
            </Thead>
            <Tbody>
              {devices.map(device => (
                <Tr key={device.tag}>
                  <Td>
                    <HStack spacing={2}>
                      <Icon as={FaPlug} color={useColorModeValue('gray.500', 'gray.400')} />
                      <Text fontFamily="mono">{device.tag}</Text>
                    </HStack>
                  </Td>
                  <Td>{device.friendly_name || '—'}</Td>
                  <Td>{`${device.card_index}.${device.device_index}`}</Td>
                  <Td>{formatChannelList(device.channels_supported)}</Td>
                  <Td>{renderSampleRates(device.sample_rates)}</Td>
                  <Td>{formatBitDepths(device)}</Td>
                  <Td>
                    <Badge
                      colorScheme={device.present ? 'green' : 'orange'}
                      borderRadius="full"
                      px={3}
                      py={1}
                    >
                      <HStack spacing={1}>
                        <Icon as={device.present ? FaCheckCircle : FaExclamationTriangle} />
                        <Text>{device.present ? 'Online' : 'Offline'}</Text>
                      </HStack>
                    </Badge>
                  </Td>
                </Tr>
              ))}
            </Tbody>
          </Table>
        </TableContainer>
      )}
    </Box>
  );
};

const SystemDevicesContent: React.FC<ContentProps> = ({
  systemCaptureDevices,
  systemPlaybackDevices,
}) => {
  const descriptionColor = useColorModeValue('gray.600', 'gray.400');

  return (
    <Stack spacing={6}>
      <Box>
        <Heading as="h2" size="lg" mb={2}>
          System Audio Devices
        </Heading>
        <Text color={descriptionColor}>
          These system audio devices are discovered automatically from the host (ALSA on Linux, WASAPI on Windows).
          They can be referenced when creating local capture and playback endpoints. Entries cannot be deleted manually;
          offline devices remain listed for reference and will become active when reconnected.
        </Text>
      </Box>

      <DeviceSection
        title="Capture Devices"
        icon={FaMicrophone}
        devices={systemCaptureDevices}
      />

      <DeviceSection
        title="Playback Devices"
        icon={FaVolumeUp}
        devices={systemPlaybackDevices}
      />

      <Divider />
      <Text fontSize="sm" color={descriptionColor}>
        Tip: Use the system audio device pickers in the add/edit dialogs to assign these tags to your routes. Routes referencing
        offline hardware will resume automatically once the device is available again.
      </Text>
    </Stack>
  );
};

export default SystemDevicesContent;

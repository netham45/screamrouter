/**
 * React component for adding or editing a sink in a standalone page.
 * This component provides a form to input details about a sink, including its name, IP address,
 * port, bit depth, sample rate, channels, channel layout, volume, delay, time sync settings, and more.
 * It allows the user to either add a new sink or update an existing one.
 */
import React, { useState, useEffect, useMemo } from 'react';
import { useSearchParams } from 'react-router-dom';
import {
  Flex,
  FormControl,
  FormLabel,
  Input,
  Select,
  Checkbox,
  Button,
  Stack,
  NumberInput,
  NumberInputField,
  NumberInputStepper,
  NumberIncrementStepper,
  NumberDecrementStepper,
  Alert,
  AlertIcon,
  Box,
  Badge,
  Heading,
  Container,
  useColorModeValue,
  Switch,
  Text,
  HStack
} from '@chakra-ui/react';
import ApiService, { Sink, SystemAudioDeviceInfo } from '../../api/api';
import { useTutorial } from '../../context/TutorialContext';
import { useMdnsDiscovery } from '../../context/MdnsDiscoveryContext';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import MultiRtpReceiverManager, { RtpReceiverMapping } from './controls/MultiRtpReceiverManager';
import { useAppContext } from '../../context/AppContext';

const AddEditSinkPage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const sinkName = searchParams.get('name');
  const isEdit = !!sinkName;

  const { completeStep, nextStep } = useTutorial();
  const { openModal: openMdnsModal, registerSelectionHandler } = useMdnsDiscovery();
  const { systemPlaybackDevices } = useAppContext();

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [sink, setSink] = useState<Sink | null>(null);
  const [name, setName] = useState('');
  const [ip, setIp] = useState('');
  const [port, setPort] = useState('4010');
  const [bitDepth, setBitDepth] = useState('32');
  const [sampleRate, setSampleRate] = useState('48000');
  const [channels, setChannels] = useState('2');
  const [channelLayout, setChannelLayout] = useState('stereo');
  const [volume, setVolume] = useState(1);
  const [delay, setDelay] = useState(0);
  const [timeshift, setTimeshift] = useState(0);
  const [timeSync, setTimeSync] = useState(false);
  const [timeSyncDelay, setTimeSyncDelay] = useState('0');
  const [protocol, setProtocol] = useState('scream');
  const [selectedPlaybackTag, setSelectedPlaybackTag] = useState('');
  const [outputMode, setOutputMode] = useState<'network' | 'system'>('network');
  const [previousNetworkConfig, setPreviousNetworkConfig] = useState<{ ip: string; port: string; protocol: string }>({
    ip: '',
    port: '4010',
    protocol: 'scream',
  });
  const [volumeNormalization, setVolumeNormalization] = useState(false);
  const [multiDeviceMode, setMultiDeviceMode] = useState(false);
  const [rtpReceiverMappings, setRtpReceiverMappings] = useState<RtpReceiverMapping[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  const selectedPlaybackDevice = useMemo<SystemAudioDeviceInfo | undefined>(() => {
    return systemPlaybackDevices.find(device => device.tag === selectedPlaybackTag);
  }, [systemPlaybackDevices, selectedPlaybackTag]);

  const normalizePlaybackTag = (value?: string | null): string => {
    if (!value) {
      return '';
    }
    if (value.startsWith('ap:')) {
      return value;
    }
    if (value.startsWith('hw:')) {
      const body = value.substring(3);
      if (body.includes(',')) {
        const [card, device] = body.split(',', 2);
        return `ap:${card}.${device}`;
      }
    }
    return value;
  };

  const formatChannelList = (channels?: number[]): string => {
    if (!channels || channels.length === 0) {
      return '—';
    }
    return channels.join(', ');
  };

  const formatSampleRateList = (rates?: number[]): string => {
    if (!rates || rates.length === 0) {
      return '—';
    }
    return rates
      .slice()
      .sort((a, b) => a - b)
      .map(rate => {
        if (rate >= 1000) {
          const khz = rate / 1000;
          return Number.isInteger(khz) ? `${khz} kHz` : `${khz.toFixed(1)} kHz`;
        }
        return `${rate} Hz`;
      })
      .join(', ');
  };

  useEffect(() => {
    if (!name.trim()) {
      return;
    }
    completeStep('sink-name-input');
  }, [name, completeStep]);

  useEffect(() => {
    if (!ip.trim()) {
      return;
    }
    completeStep('sink-ip-input');
  }, [ip, completeStep]);

  useEffect(() => {
    if (!port.trim()) {
      return;
    }
    completeStep('sink-port-input');
  }, [port, completeStep]);

  useEffect(() => {
    if (typeof window === 'undefined') {
      return;
    }
    const handleBeforeUnload = () => {
      try {
        const targetOrigin = window.location.origin;
        window.opener?.postMessage(
          {
            type: 'FORM_WINDOW_CLOSING',
            form: 'sink',
          },
          targetOrigin
        );
      } catch (error) {
        console.error('Failed to announce sink form closing', error);
      }
    };

    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => {
      window.removeEventListener('beforeunload', handleBeforeUnload);
    };
  }, []);

  // Fetch sink data if editing
  useEffect(() => {
    const fetchSink = async () => {
      if (sinkName) {
        try {
          const response = await ApiService.getSinks();
          const sinkData = Object.values(response.data).find(s => s.name === sinkName);
          if (sinkData) {
            setSink(sinkData);
            setName(sinkData.name);
            const rawIp = typeof sinkData.ip === 'string' ? sinkData.ip : '';
            const normalizedTag = normalizePlaybackTag(rawIp);
            const incomingProtocol = sinkData.protocol ? sinkData.protocol.toLowerCase() : '';
            const resolvedProtocol = incomingProtocol === 'system_audio'
              ? 'system_audio'
              : incomingProtocol || (normalizedTag ? 'system_audio' : 'scream');

            if (resolvedProtocol === 'system_audio') {
              const tagToUse = normalizedTag || rawIp;
              setSelectedPlaybackTag(tagToUse);
              setIp(tagToUse);
              setPort('0');
              setOutputMode('system');
            } else {
              setSelectedPlaybackTag('');
              setIp(rawIp);
              setPort(sinkData.port?.toString() || '4010');
              setOutputMode('network');
            }
            setBitDepth(sinkData.bit_depth?.toString() || '32');
            setSampleRate(sinkData.sample_rate?.toString() || '48000');
            setChannels(sinkData.channels?.toString() || '2');
            setChannelLayout(sinkData.channel_layout || 'stereo');
            setVolume(sinkData.volume || 1);
            setDelay(sinkData.delay || 0);
            setTimeshift(sinkData.timeshift || 0);
            setTimeSync(sinkData.time_sync || false);
            setTimeSyncDelay(sinkData.time_sync_delay?.toString() || '0');
            setProtocol(resolvedProtocol);
            setVolumeNormalization(sinkData.volume_normalization || false);
            // Set multi-device mode and mappings if they exist
            setMultiDeviceMode(sinkData.multi_device_mode || false);
            setRtpReceiverMappings(sinkData.rtp_receiver_mappings || []);

            setPreviousNetworkConfig({
              ip: resolvedProtocol === 'system_audio' ? '' : rawIp,
              port: resolvedProtocol === 'system_audio' ? '4010' : (sinkData.port?.toString() || '4010'),
              protocol: resolvedProtocol === 'system_audio' ? 'scream' : (resolvedProtocol || 'scream'),
            });
          } else {
            setError(`Sink "${sinkName}" not found.`);
          }
        } catch (error) {
          console.error('Error fetching sink:', error);
          setError('Failed to fetch sink data. Please try again.');
        }
      }
    };

    fetchSink();
  }, [sinkName]);

  useEffect(() => {
    const unregister = registerSelectionHandler(device => {
      if (device.name) {
        setName(prev => prev || device.name);
      }
      if (device.ip) {
        setIp(device.ip);
      }
      if (device.port) {
        setPort(device.port.toString());
      }

      const properties = device.properties ?? {};
      const entries = Object.entries(properties);

      const normalizeKey = (key: string): string => key.toLowerCase().replace(/[^a-z0-9]/g, '');

      const propertyMap = new Map<string, unknown>();
      entries.forEach(([key, value]) => {
        propertyMap.set(key, value);
        propertyMap.set(key.toLowerCase(), value);
        propertyMap.set(normalizeKey(key), value);
      });

      const normalizeValueToString = (value: unknown): string | undefined => {
        if (value === null || value === undefined) {
          return undefined;
        }

        if (Array.isArray(value)) {
          const joined = value
            .map(item => (item === null || item === undefined ? '' : String(item)))
            .join(',');
          return joined.trim() || undefined;
        }

        const normalized = String(value).trim();
        return normalized || undefined;
      };

      const getStringProperty = (...keys: string[]): string | undefined => {
        for (const key of keys) {
          const candidates = [
            propertyMap.get(key),
            propertyMap.get(key.toLowerCase()),
            propertyMap.get(normalizeKey(key)),
          ];
          const found = candidates.find(candidate => candidate !== undefined);
          const normalized = normalizeValueToString(found);
          if (normalized) {
            return normalized;
          }
        }
        return undefined;
      };

      const parseNumberList = (raw: string): number[] => {
        const matches = raw.match(/\d+/g);
        if (!matches) {
          return [];
        }
        return matches
          .map(match => Number.parseInt(match, 10))
          .filter(value => !Number.isNaN(value) && value > 0);
      };

      const getNumericProperty = (...keys: string[]): number | undefined => {
        const raw = getStringProperty(...keys);
        if (!raw) {
          return undefined;
        }

        const direct = Number.parseInt(raw, 10);
        if (!Number.isNaN(direct) && direct > 0) {
          return direct;
        }

        const candidates = parseNumberList(raw);
        return candidates[0];
      };

      const channelsRaw = getStringProperty('channels', 'channel_count', 'channelCount');
      if (channelsRaw) {
        const channelCandidates = parseNumberList(channelsRaw);
        const channelValue = channelCandidates.sort((a, b) => b - a)[0];
        if (channelValue) {
          setChannels(channelValue.toString());
        }
      }

      const discoveredBitDepth = getNumericProperty('bit_depth', 'bitdepth', 'bitDepth');
      if (discoveredBitDepth && discoveredBitDepth > 0) {
        setBitDepth(discoveredBitDepth.toString());
      } else {
        const bitDepthListRaw = getStringProperty(
          'bit_depths',
          'bitdepths',
          'supported_bit_depths',
          'supportedBitDepths'
        );
        if (bitDepthListRaw) {
          const bitDepthCandidates = parseNumberList(bitDepthListRaw);
          const preferenceOrder = [32, 24, 16];
          const preferredBitDepth = preferenceOrder.find(depth => bitDepthCandidates.includes(depth));
          const fallbackBitDepth = bitDepthCandidates.sort((a, b) => b - a)[0];
          const selectedBitDepth = preferredBitDepth ?? fallbackBitDepth;
          if (selectedBitDepth) {
            setBitDepth(selectedBitDepth.toString());
          }
        }
      }

      const candidateSampleRate = getNumericProperty(
        'sample_rate',
        'sample_rate_hz',
        'samplerate',
        'sampleRate'
      );

      if (candidateSampleRate && candidateSampleRate > 0) {
        setSampleRate(candidateSampleRate.toString());
      } else {
        const sampleRateListRaw = getStringProperty(
          'sample_rates',
          'samplerates',
          'available_sample_rates',
          'availableSamplerates',
          'availableSampleRates',
          'supported_sample_rates',
          'supportedSampleRates'
        );
        if (sampleRateListRaw) {
          const parsedRates = parseNumberList(sampleRateListRaw);
          if (parsedRates.length > 0) {
            const preferredRate = parsedRates.find(rate => rate === 48000) ?? parsedRates[0];
            setSampleRate(preferredRate.toString());
          }
        }
      }

      const knownProtocols: Record<string, string> = {
        scream: 'scream',
        rtp: 'rtp',
        'web_receiver': 'web_receiver',
        webreceiver: 'web_receiver',
        web: 'web_receiver',
      };

      const protocolFromProperties = getStringProperty('protocol', 'transport', 'format');
      let normalizedProtocol: string | undefined;

      if (protocolFromProperties) {
        const lowered = protocolFromProperties.toLowerCase();
        normalizedProtocol = knownProtocols[lowered];
      }

      if (!normalizedProtocol) {
        const method = (device.discovery_method || '').toLowerCase();
        if (method.includes('rtp')) {
          normalizedProtocol = 'rtp';
        } else if (method.includes('web')) {
          normalizedProtocol = 'web_receiver';
        } else if (method.includes('scream') || method.includes('mdns')) {
          normalizedProtocol = 'scream';
        }
      }

      if (normalizedProtocol) {
        setProtocol(normalizedProtocol);
      }
    });

    return unregister;
  }, [registerSelectionHandler]);

  /**
   * Handles form submission to add or update a sink.
   * Validates input and sends the data to the API service.
   */
  useEffect(() => {
    if (outputMode === 'system') {
      setProtocol('system_audio');
      setPort('0');
      setIp(selectedPlaybackTag || '');
    } else {
      setProtocol(prev => (prev === 'system_audio' ? previousNetworkConfig.protocol || 'scream' : prev));
      setIp(previousNetworkConfig.ip || '');
      setPort(previousNetworkConfig.port || '4010');
    }
  }, [outputMode, previousNetworkConfig, selectedPlaybackTag]);

  const handleSubmit = async () => {
    const trimmedName = name.trim();
    if (!trimmedName) {
      setError('Sink name is required.');
      return;
    }

    if (outputMode === 'network') {
      if (!ip.trim()) {
        setError('Please provide an IP address for the sink.');
        return;
      }
      if (!port.trim()) {
        setError('Port is required for network sinks.');
        return;
      }
    } else if (!selectedPlaybackTag) {
      setError('Select a system audio playback device for this sink.');
      return;
    }

    // Ensure numeric values are valid
    const portNum = outputMode === 'system' ? 0 : parseInt(port);
    const bitDepthNum = parseInt(bitDepth) || 32;
    const sampleRateNum = parseInt(sampleRate) || 48000;
    const channelsNum = parseInt(channels) || 2;
    const timeSyncDelayNum = parseInt(timeSyncDelay) || 0;

    if (outputMode === 'network') {
      if (isNaN(portNum) || portNum < 1 || portNum > 65535) {
        setError('Port must be a valid number between 1 and 65535');
        return;
      }
    }

    if (isNaN(channelsNum) || channelsNum < 1 || channelsNum > 8) {
      setError('Channels must be a number between 1 and 8');
      return;
    }

    const sinkData: Partial<Sink> = {
      name: trimmedName,
      ip: outputMode === 'system' ? selectedPlaybackTag : ip.trim(),
      port: portNum,
      bit_depth: bitDepthNum,
      sample_rate: sampleRateNum,
      channels: channelsNum,
      channel_layout: channelLayout || 'stereo',
      volume: volume || 1,
      delay: delay || 0,
      time_sync: timeSync || false,
      time_sync_delay: timeSyncDelayNum,
      protocol: outputMode === 'system' ? 'system_audio' : (protocol || 'scream'),
      volume_normalization: volumeNormalization || false,
      enabled: false,  // New sinks start disabled by default
      is_group: false,
      group_members: [],
      equalizer: {
        b1: 1, b2: 1, b3: 1, b4: 1, b5: 1, b6: 1,
        b7: 1, b8: 1, b9: 1, b10: 1, b11: 1, b12: 1,
        b13: 1, b14: 1, b15: 1, b16: 1, b17: 1, b18: 1,
        normalization_enabled: false
      },
      timeshift: timeshift
    };

    // Add multi-device mode configuration if protocol is RTP
    if (protocol === 'rtp' && outputMode === 'network') {
      sinkData.multi_device_mode = multiDeviceMode;
      if (multiDeviceMode && rtpReceiverMappings.length > 0) {
        // Validate receiver mappings
        const validMappings = rtpReceiverMappings.filter(mapping =>
          mapping.receiver_sink_name && mapping.receiver_sink_name.trim() !== ''
        );
        if (validMappings.length > 0) {
          sinkData.rtp_receiver_mappings = validMappings;
        }
      }
    }

    // Log the data being sent for debugging
    console.log('Submitting sink data:', JSON.stringify(sinkData, null, 2));

    try {
      if (isEdit) {
        console.log(`Updating sink: ${sinkName}`);
        // For updates, only send the fields that have changed or are being updated
        const updateData: Partial<Sink> = {
          name: sinkData.name,
          ip: sinkData.ip,
          port: sinkData.port,
          bit_depth: sinkData.bit_depth,
          sample_rate: sinkData.sample_rate,
          channels: sinkData.channels,
          channel_layout: sinkData.channel_layout,
          volume: sinkData.volume,
          delay: sinkData.delay,
          time_sync: sinkData.time_sync,
          time_sync_delay: sinkData.time_sync_delay,
          protocol: sinkData.protocol,
          volume_normalization: sinkData.volume_normalization,
          multi_device_mode: sinkData.multi_device_mode,
          rtp_receiver_mappings: sinkData.rtp_receiver_mappings,
        };
        
        await ApiService.updateSink(sinkName!, updateData);
        setSuccess(`Sink "${name}" updated successfully.`);
        setError(null);
      } else {
        console.log('Adding new sink');
        await ApiService.addSink(sinkData as Sink);
        setSuccess(`Sink "${name}" added successfully.`);
        setError(null);
      }

      completeStep('sink-submit');
      nextStep();

      if (outputMode === 'network') {
        setPreviousNetworkConfig({
          ip: (sinkData.ip as string) || '',
          port: sinkData.port?.toString() || '4010',
          protocol: sinkData.protocol || 'scream',
        });
      } else {
        setPreviousNetworkConfig({ ip: '', port: '4010', protocol: 'scream' });
      }

      try {
        const targetOrigin = window.location.origin;
        window.opener?.postMessage(
          {
            type: 'RESOURCE_ADDED',
            resourceType: 'sink',
            action: isEdit ? 'updated' : 'added',
            name,
          },
          targetOrigin
        );
      } catch (messageError) {
        console.error('Failed to post resource update message', messageError);
      }
      
      // Clear form if adding a new sink
      if (!isEdit) {
        // Reset all form fields
        setName('');
        setIp('');
        setPort('4010');
        setBitDepth('32');
        setSampleRate('48000');
        setChannels('2');
        setChannelLayout('stereo');
        setVolume(1);
        setDelay(0);
        setTimeshift(0);
        setTimeSync(false);
        setTimeSyncDelay('0');
        setProtocol('scream');
        setOutputMode('network');
        setSelectedPlaybackTag('');
        setPreviousNetworkConfig({ ip: '', port: '4010', protocol: 'scream' });
        setVolumeNormalization(false);
        setMultiDeviceMode(false);
        setRtpReceiverMappings([]);
        
        // Clear the success message after a delay
        setTimeout(() => {
          setSuccess(null);
        }, 3000);
      }

      if (window.opener) {
        setTimeout(() => {
          try {
            window.close();
          } catch (closeError) {
            console.error('Failed to close window after sink submission', closeError);
          }
        }, 200);
      }
    } catch (error: any) {
      console.error('Full error object:', error);
      console.error('Error response:', error.response);
      console.error('Error response data:', error.response?.data);
      
      let errorMessage = 'Failed to submit sink. ';
      
      if (error.response?.data) {
        const errorData = error.response.data;
        console.error('Error data structure:', errorData);
        
        // Handle FastAPI validation error format
        if (Array.isArray(errorData)) {
          // Array of validation errors
          const fieldErrors = errorData.map((err: any) => {
            const field = err.loc ? err.loc[err.loc.length - 1] : 'unknown field';
            const msg = err.msg || 'Field required';
            return `${field}: ${msg}`;
          });
          errorMessage = 'Validation errors:\n' + fieldErrors.join('\n');
        } else if (errorData.detail) {
          if (typeof errorData.detail === 'string') {
            errorMessage = errorData.detail;
          } else if (Array.isArray(errorData.detail)) {
            // Handle array of error details from FastAPI
            const fieldErrors = errorData.detail.map((err: any) => {
              if (err.loc && Array.isArray(err.loc)) {
                const field = err.loc[err.loc.length - 1];
                const msg = err.msg || 'Field required';
                const type = err.type || '';
                return `Field "${field}": ${msg}${type ? ` (${type})` : ''}`;
              }
              return err.msg || err.message || JSON.stringify(err);
            });
            errorMessage = 'The following fields have errors:\n' + fieldErrors.join('\n');
          } else {
            errorMessage = JSON.stringify(errorData.detail);
          }
        } else {
          // Fallback to showing the raw error data
          errorMessage = 'Raw error: ' + JSON.stringify(errorData);
        }
      } else if (error.message) {
        errorMessage += error.message;
      } else {
        errorMessage += 'Unknown error occurred';
      }
      
      console.error('Final error message:', errorMessage);
      setError(errorMessage);
      setSuccess(null);
    }
  };

  const handleClose = () => {
    window.close();
  };

  return (
    <Container maxW="container.md" py={8}>
      <Box
        bg={bgColor}
        borderColor={borderColor}
        borderWidth="1px"
        borderRadius="lg"
        p={6}
        boxShadow="md"
      >
        <Heading as="h1" size="lg" mb={6}>
          {isEdit ? 'Edit Sink' : 'Add Sink'}
        </Heading>
        
        {error && (
          <Alert status="error" mb={4} borderRadius="md">
            <AlertIcon />
            {error}
          </Alert>
        )}
        
        {success && (
          <Alert status="success" mb={4} borderRadius="md">
            <AlertIcon />
            {success}
          </Alert>
        )}
        
        <Stack spacing={4}>
          <FormControl isRequired>
            <FormLabel>Sink Name</FormLabel>
            <Input
              data-tutorial-id="sink-name-input"
              value={name}
              onChange={(e) => setName(e.target.value)}
              bg={inputBg}
            />
          </FormControl>
          
          <FormControl>
            <FormLabel>Output Type</FormLabel>
            <Select
              value={outputMode}
              onChange={(event) => {
                const nextMode = event.target.value as 'network' | 'system';
                if (nextMode === 'system') {
                  setPreviousNetworkConfig({
                    ip,
                    port,
                    protocol,
                  });
                }
                setOutputMode(nextMode);
                if (nextMode === 'network' && protocol === 'system_audio') {
                  setProtocol(previousNetworkConfig.protocol || 'scream');
                }
              }}
              bg={inputBg}
            >
              <option value="network">Network Sink (IP)</option>
              <option value="system">System Audio Device</option>
            </Select>
          </FormControl>

          <FormControl isRequired={outputMode === 'network'}>
            <FormLabel>{outputMode === 'system' ? 'Playback Tag' : 'Sink IP'}</FormLabel>
            <Stack
              direction={{ base: 'column', md: 'row' }}
              spacing={2}
              align={{ base: 'stretch', md: 'center' }}
            >
              <Input
                data-tutorial-id="sink-ip-input"
                value={outputMode === 'system' ? (selectedPlaybackTag || '') : ip}
                onChange={(e) => {
                  if (outputMode === 'network') {
                    setIp(e.target.value);
                  }
                }}
                bg={inputBg}
                flex="1"
                isReadOnly={outputMode === 'system'}
                placeholder={outputMode === 'system' ? 'Select a system audio playback device' : 'Enter the sink address'}
              />
              <Button
                onClick={() => openMdnsModal('sinks')}
                variant="outline"
                colorScheme="blue"
                width={{ base: '100%', md: 'auto' }}
                isDisabled={outputMode === 'system'}
              >
                Discover Devices
              </Button>
            </Stack>
            {outputMode === 'system' && systemPlaybackDevices.length === 0 && (
              <Text mt={2} fontSize="sm" color="orange.500">
                No system playback devices detected. Connect a system audio output to select it here.
              </Text>
            )}
          </FormControl>

          {outputMode === 'system' && (
            <FormControl isRequired>
              <FormLabel>System Playback Device</FormLabel>
              <Select
                value={selectedPlaybackTag}
                onChange={(event) => setSelectedPlaybackTag(event.target.value)}
                placeholder={systemPlaybackDevices.length > 0 ? 'Select a system audio playback device' : 'No devices available'}
                bg={inputBg}
                isDisabled={systemPlaybackDevices.length === 0}
              >
                {systemPlaybackDevices.map(device => (
                  <option key={device.tag} value={device.tag}>
                    {device.friendly_name || device.tag} ({device.tag}){device.present ? '' : ' • offline'}
                  </option>
                ))}
              </Select>
              {selectedPlaybackDevice && (
                <Box
                  mt={2}
                  p={3}
                  borderWidth="1px"
                  borderRadius="md"
                  bg={useColorModeValue('gray.50', 'gray.700')}
                  borderColor={useColorModeValue('gray.200', 'gray.600')}
                >
                  <HStack spacing={3} align="center" mb={1}>
                    <Badge colorScheme={selectedPlaybackDevice.present ? 'green' : 'orange'}>
                      {selectedPlaybackDevice.present ? 'Online' : 'Offline'}
                    </Badge>
                    <Text fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                      Tag: {selectedPlaybackDevice.tag}
                    </Text>
                  </HStack>
                  <Text fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                    Channels: {formatChannelList(selectedPlaybackDevice.channels_supported)}
                  </Text>
                  <Text fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                    Sample Rates: {formatSampleRateList(selectedPlaybackDevice.sample_rates)}
                  </Text>
                  {!selectedPlaybackDevice.present && (
                    <Text fontSize="sm" color="orange.500" mt={2}>
                      This device is currently offline. Routes will activate automatically when it becomes available.
                    </Text>
                  )}
                </Box>
              )}
            </FormControl>
          )}
          
          <FormControl isRequired={outputMode === 'network'}>
            <FormLabel>Sink Port</FormLabel>
            <NumberInput
              data-tutorial-id="sink-port-input"
              value={port}
              onChange={(valueString) => setPort(valueString)}
              min={outputMode === 'system' ? 0 : 1}
              max={65535}
              bg={inputBg}
              isDisabled={outputMode === 'system'}
            >
              <NumberInputField />
              <NumberInputStepper>
                <NumberIncrementStepper />
                <NumberDecrementStepper />
              </NumberInputStepper>
            </NumberInput>
            {outputMode === 'system' && (
              <Text mt={2} fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                System audio sinks do not require a network port. The engine will manage the device directly.
              </Text>
            )}
          </FormControl>
          
          <FormControl>
            <FormLabel>Bit Depth</FormLabel>
            <Select
              data-tutorial-id="sink-bit-depth-select"
              value={bitDepth}
              onChange={(e) => setBitDepth(e.target.value)}
              bg={inputBg}
            >
              <option value="16">16</option>
              <option value="24">24</option>
              <option value="32">32</option>
            </Select>
          </FormControl>
          
          <FormControl>
            <FormLabel>Sample Rate</FormLabel>
            <Select
              data-tutorial-id="sink-sample-rate-select"
              value={sampleRate}
              onChange={(e) => setSampleRate(e.target.value)}
              bg={inputBg}
            >
              <option value="44100">44100</option>
              <option value="48000">48000</option>
              <option value="88200">88200</option>
              <option value="96000">96000</option>
              <option value="192000">192000</option>
            </Select>
          </FormControl>
          
          <FormControl isRequired>
            <FormLabel>Channels</FormLabel>
            <NumberInput
              data-tutorial-id="sink-channels-input"
              value={channels}
              onChange={(valueString) => setChannels(valueString)}
              min={1}
              max={8}
              bg={inputBg}
            >
              <NumberInputField />
              <NumberInputStepper>
                <NumberIncrementStepper />
                <NumberDecrementStepper />
              </NumberInputStepper>
            </NumberInput>
          </FormControl>
          
          <FormControl>
            <FormLabel>Channel Layout</FormLabel>
            <Select
              data-tutorial-id="sink-channel-layout-select"
              value={channelLayout}
              onChange={(e) => setChannelLayout(e.target.value)}
              bg={inputBg}
            >
              <option value="mono">Mono</option>
              <option value="stereo">Stereo</option>
              <option value="quad">Quad</option>
              <option value="surround">Surround</option>
              <option value="5.1">5.1</option>
              <option value="7.1">7.1</option>
            </Select>
          </FormControl>

          {outputMode === 'network' ? (
            <FormControl>
              <FormLabel>Protocol</FormLabel>
              <Select
                data-tutorial-id="sink-protocol-select"
                value={protocol}
                onChange={(e) => setProtocol(e.target.value)}
                bg={inputBg}
              >
                <option value="scream">Scream</option>
                <option value="rtp">RTP</option>
                <option value="web_receiver">Web Receiver</option>
              </Select>
            </FormControl>
          ) : (
            <FormControl>
              <FormLabel>Protocol</FormLabel>
              <Input value="system_audio" isReadOnly bg={inputBg} />
            </FormControl>
          )}

          {/* Multi-Device Mode for RTP Protocol */}
          {protocol === 'rtp' && outputMode === 'network' && (
            <>
              <FormControl>
                <Flex alignItems="center">
                  <FormLabel htmlFor="multi-device-mode" mb={0} mr={3}>
                    Multi-Device Mode
                  </FormLabel>
                  <Switch
                    id="multi-device-mode"
                    data-tutorial-id="sink-multi-device-toggle"
                    isChecked={multiDeviceMode}
                    onChange={(e) => {
                      setMultiDeviceMode(e.target.checked);
                      // Clear mappings when disabling multi-device mode
                      if (!e.target.checked) {
                        setRtpReceiverMappings([]);
                      }
                    }}
                  />
                </Flex>
                <Text fontSize="sm" color="gray.500" mt={1}>
                  Enable to send different channel pairs to multiple RTP receivers
                </Text>
              </FormControl>

              {/* RTP Receiver Mappings */}
              {multiDeviceMode && (
                <FormControl>
                  <FormLabel>RTP Receiver Mappings</FormLabel>
                  <Box
                    borderWidth="1px"
                    borderRadius="md"
                    p={4}
                    data-tutorial-id="sink-rtp-mappings"
                  >
                    <MultiRtpReceiverManager
                      receivers={rtpReceiverMappings}
                      onReceiversChange={setRtpReceiverMappings}
                    />
                  </Box>
                </FormControl>
              )}
            </>
          )}
          
          <FormControl>
            <FormLabel>Volume</FormLabel>
            <VolumeSlider
              value={volume}
              onChange={setVolume}
              dataTutorialId="sink-volume-slider"
            />
          </FormControl>
          
          <FormControl>
            <FormLabel>Delay (ms)</FormLabel>
            <NumberInput
              data-tutorial-id="sink-delay-input"
              value={delay}
              onChange={(valueString) => {
                const parsed = Number.parseInt(valueString, 10);
                setDelay(Number.isNaN(parsed) ? 0 : parsed);
              }}
              min={0}
              max={5000}
              bg={inputBg}
            >
              <NumberInputField />
              <NumberInputStepper>
                <NumberIncrementStepper />
                <NumberDecrementStepper />
              </NumberInputStepper>
            </NumberInput>
          </FormControl>
          
          <FormControl>
            <FormLabel>Timeshift</FormLabel>
            <TimeshiftSlider
              value={timeshift}
              onChange={setTimeshift}
              dataTutorialId="sink-timeshift-slider"
            />
          </FormControl>

          <FormControl>
            <Flex alignItems="center">
              <Checkbox
                data-tutorial-id="sink-volume-normalization-checkbox"
                isChecked={volumeNormalization}
                onChange={(e) => setVolumeNormalization(e.target.checked)}
                mr={2}
              />
              <FormLabel mb={0}>Volume Normalization</FormLabel>
            </Flex>
          </FormControl>
          
          <FormControl>
            <Flex alignItems="center">
              <Checkbox
                data-tutorial-id="sink-time-sync-checkbox"
                isChecked={timeSync}
                onChange={(e) => setTimeSync(e.target.checked)}
                mr={2}
              />
              <FormLabel mb={0}>Time Sync</FormLabel>
            </Flex>
          </FormControl>
          
          <FormControl>
            <FormLabel>Time Sync Delay (ms)</FormLabel>
            <NumberInput
              data-tutorial-id="sink-time-sync-delay-input"
              value={timeSyncDelay}
              onChange={(valueString) => setTimeSyncDelay(valueString)}
              min={0}
              max={5000}
              bg={inputBg}
              isDisabled={!timeSync}
            >
              <NumberInputField />
              <NumberInputStepper>
                <NumberIncrementStepper />
                <NumberDecrementStepper />
              </NumberInputStepper>
            </NumberInput>
          </FormControl>
        </Stack>
        
        <Flex mt={8} gap={3} justifyContent="flex-end">
          <Button
            colorScheme="blue"
            onClick={handleSubmit}
            data-tutorial-id="sink-submit-button"
          >
            {isEdit ? 'Update Sink' : 'Add Sink'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditSinkPage;

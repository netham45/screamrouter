/**
 * React component for adding or editing a source in a standalone page.
 * This component provides a form to input details about a source, including its name, IP address,
 * volume, delay, timeshift, and VNC settings.
 * It allows the user to either add a new source or update an existing one.
 */
import React, { useState, useEffect, useMemo } from 'react';
import { useSearchParams } from 'react-router-dom';
import {
  Flex,
  FormControl,
  FormLabel,
  Input,
  Select,
  Button,
  Stack,
  SimpleGrid,
  NumberInput,
  NumberInputField,
  NumberInputStepper,
  NumberIncrementStepper,
  NumberDecrementStepper,
  Alert,
  AlertIcon,
  Box,
  Badge,
  HStack,
  Heading,
  Container,
  useColorModeValue,
  Switch,
  Text,
  Textarea
} from '@chakra-ui/react';
import ApiService, { Source, SystemAudioDeviceInfo } from '../../api/api';
import { useTutorial } from '../../context/TutorialContext';
import { useMdnsDiscovery } from '../../context/MdnsDiscoveryContext';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import { useAppContext } from '../../context/AppContext';

const CAPTURE_SAMPLE_RATE_OPTIONS = [44100, 48000, 96000];
const CAPTURE_BIT_DEPTH_OPTIONS = [16, 24, 32];
const CAPTURE_DEFAULT_CHANNELS = 2;
const CAPTURE_DEFAULT_SAMPLE_RATE = 48000;
const CAPTURE_DEFAULT_BIT_DEPTH = 32;

const AddEditSourcePage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const sourceName = searchParams.get('name');
  const isEdit = !!sourceName;

  const { completeStep, nextStep } = useTutorial();
  const { openModal: openMdnsModal, registerSelectionHandler } = useMdnsDiscovery();
  const { systemCaptureDevices } = useAppContext();

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [source, setSource] = useState<Source | null>(null);
  const [name, setName] = useState('');
  const [ip, setIp] = useState('');
  const [inputMode, setInputMode] = useState<'network' | 'system'>('network');
  const [selectedCaptureTag, setSelectedCaptureTag] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [isGroup, setIsGroup] = useState(false);
  const [groupMembers, setGroupMembers] = useState<string[]>([]);
  const [groupMembersText, setGroupMembersText] = useState('');
  const [volume, setVolume] = useState(1);
  const [delay, setDelay] = useState(0);
  const [timeshift, setTimeshift] = useState(0);
  const [captureChannels, setCaptureChannels] = useState<number>(CAPTURE_DEFAULT_CHANNELS);
  const [captureSampleRate, setCaptureSampleRate] = useState<number>(CAPTURE_DEFAULT_SAMPLE_RATE);
  const [captureBitDepth, setCaptureBitDepth] = useState<number>(CAPTURE_DEFAULT_BIT_DEPTH);
  const [vncIp, setVncIp] = useState('');
  const [vncPort, setVncPort] = useState('5900');
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  const selectedCaptureDevice = useMemo<SystemAudioDeviceInfo | undefined>(() => {
    return systemCaptureDevices.find(device => device.tag === selectedCaptureTag);
  }, [systemCaptureDevices, selectedCaptureTag]);

  useEffect(() => {
    if (!selectedCaptureDevice) {
      return;
    }

    if (selectedCaptureDevice.channels_supported?.length) {
      const supportedChannels = selectedCaptureDevice.channels_supported;
      const suggestedChannels = supportedChannels[0];
      if (suggestedChannels && !supportedChannels.includes(captureChannels)) {
        setCaptureChannels(suggestedChannels);
      }
    }

    const allowedDeviceRates = (selectedCaptureDevice.sample_rates || []).filter(rate =>
      CAPTURE_SAMPLE_RATE_OPTIONS.includes(rate)
    );
    if (allowedDeviceRates.length > 0 && !CAPTURE_SAMPLE_RATE_OPTIONS.includes(captureSampleRate)) {
      setCaptureSampleRate(allowedDeviceRates[0]);
    }
  }, [selectedCaptureDevice, captureChannels, captureSampleRate]);

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
    completeStep('source-name-input');
  }, [name, completeStep]);

  useEffect(() => {
    if (!ip.trim()) {
      return;
    }
    completeStep('source-ip-input');
  }, [ip, completeStep]);

  useEffect(() => {
    if (isGroup) {
      setInputMode('network');
      setSelectedCaptureTag('');
    }
  }, [isGroup]);

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
            form: 'source',
          },
          targetOrigin
        );
      } catch (error) {
        console.error('Failed to announce form window closing', error);
      }
    };

    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => {
      window.removeEventListener('beforeunload', handleBeforeUnload);
    };
  }, []);

  // Fetch source data if editing
  useEffect(() => {
    const fetchSource = async () => {
      if (sourceName) {
        try {
          const response = await ApiService.getSources();
          const sourceData = Object.values(response.data).find(s => s.name === sourceName);
          if (sourceData) {
            setSource(sourceData);
            setName(sourceData.name);
            setEnabled(sourceData.enabled);
            setIsGroup(sourceData.is_group);
            setGroupMembers(sourceData.group_members || []);
            setGroupMembersText((sourceData.group_members || []).join(', '));
            setVolume(sourceData.volume || 1);
            setDelay(sourceData.delay || 0);
            setTimeshift(sourceData.timeshift || 0);
            setCaptureChannels(typeof sourceData.channels === 'number' ? sourceData.channels : CAPTURE_DEFAULT_CHANNELS);
            setCaptureSampleRate(typeof sourceData.sample_rate === 'number' && CAPTURE_SAMPLE_RATE_OPTIONS.includes(sourceData.sample_rate)
              ? sourceData.sample_rate
              : CAPTURE_DEFAULT_SAMPLE_RATE);
            setCaptureBitDepth(typeof sourceData.bit_depth === 'number' ? sourceData.bit_depth : CAPTURE_DEFAULT_BIT_DEPTH);
            setVncIp(sourceData.vnc_ip || '');
            setVncPort(sourceData.vnc_port?.toString() || '5900');

            const networkIp = (typeof sourceData.ip === 'string' ? sourceData.ip : '') || '';
            const detectedCaptureTag = !sourceData.is_group && typeof sourceData.tag === 'string' && sourceData.tag.startsWith('ac:')
              ? sourceData.tag
              : (!sourceData.is_group && typeof sourceData.ip === 'string' && sourceData.ip.startsWith('ac:') ? sourceData.ip : '');

            if (!sourceData.is_group && detectedCaptureTag) {
              setInputMode('system');
              setSelectedCaptureTag(detectedCaptureTag);
              setIp('');
            } else {
              setInputMode('network');
              setSelectedCaptureTag('');
              setIp(networkIp);
            }
          } else {
            setError(`Source "${sourceName}" not found.`);
          }
        } catch (error) {
          console.error('Error fetching source:', error);
          setError('Failed to fetch source data. Please try again.');
        }
      }
    };

    fetchSource();
  }, [sourceName]);

  useEffect(() => {
    const unregister = registerSelectionHandler(device => {
      if (device.name) {
        setName(prev => prev || device.name);
      }
      if (device.ip) {
        setIp(device.ip);
      }
    });

    return unregister;
  }, [registerSelectionHandler]);

  const handleGroupMembersChange = (value: string) => {
    setGroupMembersText(value);
    const members = value
      .split(',')
      .map(member => member.trim())
      .filter(member => member.length > 0);
    setGroupMembers(members);
  };

  /**
   * Handles form submission to add or update a source.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    const trimmedName = name.trim();
    if (!trimmedName) {
      setError('Source name is required.');
      return;
    }

    if (!isGroup) {
      if (inputMode === 'network' && !ip.trim()) {
        setError('Please provide an IP address for the source.');
        return;
      }

      if (inputMode === 'system' && !selectedCaptureTag) {
        setError('Select a system audio capture device for this source.');
        return;
      }
    }

    const sourceData: Partial<Source> = {
      name: trimmedName,
      enabled,
      is_group: isGroup,
      group_members: groupMembers,
      volume,
      delay,
      timeshift,
      vnc_ip: vncIp || undefined,
      vnc_port: vncIp ? parseInt(vncPort) : undefined
    };

    if (!isGroup) {
      if (inputMode === 'system') {
        if (captureChannels < 1 || captureChannels > 8) {
          setError('Capture channels must be between 1 and 8.');
          return;
        }

        if (!CAPTURE_SAMPLE_RATE_OPTIONS.includes(captureSampleRate)) {
          setError('Select a valid capture sample rate (44.1 kHz, 48 kHz, or 96 kHz).');
          return;
        }

        if (!CAPTURE_BIT_DEPTH_OPTIONS.includes(captureBitDepth)) {
          setError('Select a valid capture bit depth.');
          return;
        }

        sourceData.channels = captureChannels;
        sourceData.sample_rate = captureSampleRate;
        sourceData.bit_depth = captureBitDepth;
        sourceData.tag = selectedCaptureTag;
        sourceData.ip = null; // clear existing network address if switching to system audio
      } else {
        sourceData.ip = ip.trim();
        sourceData.tag = null;
        sourceData.channels = null;
        sourceData.sample_rate = null;
        sourceData.bit_depth = null;
      }
    } else {
      sourceData.ip = null;
      sourceData.tag = null;
      sourceData.channels = null;
      sourceData.sample_rate = null;
      sourceData.bit_depth = null;
    }

    try {
      setError(null);
      setSuccess(null);
      if (isEdit) {
        await ApiService.updateSource(sourceName!, sourceData);
        setSuccess(`Source "${name}" updated successfully.`);
      } else {
        await ApiService.addSource(sourceData as Source);
        setSuccess(`Source "${name}" added successfully.`);
      }

      completeStep('source-submit');
      nextStep();

      try {
        const targetOrigin = window.location.origin;
        window.opener?.postMessage(
          {
            type: 'RESOURCE_ADDED',
            resourceType: 'source',
            action: isEdit ? 'updated' : 'added',
            name,
          },
          targetOrigin
        );
      } catch (messageError) {
        console.error('Failed to post resource update message', messageError);
      }

      // Clear form if adding a new source
      if (!isEdit) {
        setName('');
        setIp('');
        setInputMode('network');
        setSelectedCaptureTag('');
        setEnabled(true);
        setIsGroup(false);
        setGroupMembers([]);
        setGroupMembersText('');
        setVolume(1);
        setDelay(0);
        setTimeshift(0);
        setVncIp('');
        setVncPort('5900');
        setCaptureChannels(CAPTURE_DEFAULT_CHANNELS);
        setCaptureSampleRate(CAPTURE_DEFAULT_SAMPLE_RATE);
        setCaptureBitDepth(CAPTURE_DEFAULT_BIT_DEPTH);
      }
      if (window.opener) {
        setTimeout(() => {
          try {
            window.close();
          } catch (closeError) {
            console.error('Failed to close window after source submission', closeError);
          }
        }, 200);
      }
    } catch (error) {
      console.error('Error submitting source:', error);
      setError('Failed to submit source. Please try again.');
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
          {isEdit ? 'Edit Source' : 'Add Source'}
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
            <FormLabel>Source Name</FormLabel>
            <Input
              data-tutorial-id="source-name-input"
              value={name}
              onChange={(e) => setName(e.target.value)}
              bg={inputBg}
            />
          </FormControl>

          {!isGroup && (
            <FormControl>
              <FormLabel>Input Type</FormLabel>
              <Select
                value={inputMode}
                onChange={(event) => {
                  const mode = event.target.value as 'network' | 'system';
                  setInputMode(mode);
                  if (mode === 'network') {
                    setSelectedCaptureTag('');
                    setCaptureChannels(CAPTURE_DEFAULT_CHANNELS);
                    setCaptureSampleRate(CAPTURE_DEFAULT_SAMPLE_RATE);
                    setCaptureBitDepth(CAPTURE_DEFAULT_BIT_DEPTH);
                  }
                }}
                bg={inputBg}
              >
                <option value="network">Network Source (IP)</option>
                <option value="system">System Audio Device</option>
              </Select>
            </FormControl>
          )}

          <FormControl isRequired={!isGroup && inputMode === 'network'}>
            <FormLabel>{inputMode === 'system' ? 'Source Tag' : 'Source IP'}</FormLabel>
            <Stack
              direction={{ base: 'column', md: 'row' }}
              spacing={2}
              align={{ base: 'stretch', md: 'center' }}
            >
              <Input
                data-tutorial-id="source-ip-input"
                value={inputMode === 'system' ? (selectedCaptureTag || '') : ip}
                onChange={(e) => {
                  if (inputMode === 'network') {
                    setIp(e.target.value);
                  }
                }}
                bg={inputBg}
                flex="1"
                isReadOnly={inputMode === 'system'}
                placeholder={inputMode === 'system' ? 'Select a system audio capture device' : 'Enter the source address'}
              />
              <Button
                onClick={() => openMdnsModal('sources')}
                variant="outline"
                colorScheme="blue"
                width={{ base: '100%', md: 'auto' }}
                isDisabled={inputMode === 'system'}
              >
                Discover Devices
              </Button>
            </Stack>
            {inputMode === 'system' && systemCaptureDevices.length === 0 && (
              <Text mt={2} fontSize="sm" color="orange.500">
                No system capture devices detected. Connect a system audio source to select it here.
              </Text>
            )}
          </FormControl>

          {inputMode === 'system' && !isGroup && (
            <>
              <FormControl isRequired>
                <FormLabel>System Capture Device</FormLabel>
                <Select
                  value={selectedCaptureTag}
                  onChange={(event) => setSelectedCaptureTag(event.target.value)}
                  placeholder={systemCaptureDevices.length > 0 ? 'Select a system audio capture device' : 'No devices available'}
                  bg={inputBg}
                  isDisabled={systemCaptureDevices.length === 0}
                >
                  {systemCaptureDevices.map(device => (
                    <option key={device.tag} value={device.tag}>
                      {device.friendly_name || device.tag} ({device.tag}){device.present ? '' : ' • offline'}
                    </option>
                  ))}
                </Select>
                {selectedCaptureDevice && (
                  <Box
                    mt={2}
                    p={3}
                    borderWidth="1px"
                    borderRadius="md"
                    bg={useColorModeValue('gray.50', 'gray.700')}
                    borderColor={useColorModeValue('gray.200', 'gray.600')}
                  >
                    <HStack spacing={3} align="center" mb={1}>
                      <Badge colorScheme={selectedCaptureDevice.present ? 'green' : 'orange'}>
                        {selectedCaptureDevice.present ? 'Online' : 'Offline'}
                      </Badge>
                      <Text fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                        Tag: {selectedCaptureDevice.tag}
                      </Text>
                    </HStack>
                    <Text fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                      Channels: {formatChannelList(selectedCaptureDevice.channels_supported)}
                    </Text>
                    <Text fontSize="sm" color={useColorModeValue('gray.600', 'gray.300')}>
                      Sample Rates: {formatSampleRateList(selectedCaptureDevice.sample_rates)}
                    </Text>
                    {!selectedCaptureDevice.present && (
                      <Text fontSize="sm" color="orange.500" mt={2}>
                        This device is currently offline. Routes will activate automatically when it becomes available.
                      </Text>
                    )}
                  </Box>
                )}
              </FormControl>

              <SimpleGrid columns={{ base: 1, md: 2, xl: 3 }} spacing={4}>
                <FormControl>
                  <FormLabel>Capture Channels</FormLabel>
                  <NumberInput
                    value={captureChannels}
                    onChange={(_, valueNumber) => {
                      if (Number.isNaN(valueNumber)) {
                        setCaptureChannels(CAPTURE_DEFAULT_CHANNELS);
                        return;
                      }
                      const clamped = Math.min(Math.max(Math.round(valueNumber), 1), 8);
                      setCaptureChannels(clamped);
                    }}
                    min={1}
                    max={8}
                    step={1}
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
                  <FormLabel>Capture Sample Rate</FormLabel>
                  <Select
                    value={captureSampleRate.toString()}
                    onChange={(event) => {
                      const parsed = Number.parseInt(event.target.value, 10);
                      if (CAPTURE_SAMPLE_RATE_OPTIONS.includes(parsed)) {
                        setCaptureSampleRate(parsed);
                      }
                    }}
                    bg={inputBg}
                  >
                    {CAPTURE_SAMPLE_RATE_OPTIONS.map((rate) => (
                      <option key={rate} value={rate.toString()}>
                        {rate === 44100 ? '44.1 kHz' : `${rate / 1000} kHz`}
                      </option>
                    ))}
                  </Select>
                </FormControl>

                <FormControl>
                  <FormLabel>Capture Bit Depth</FormLabel>
                  <Select
                    value={captureBitDepth.toString()}
                    onChange={(event) => {
                      const parsed = Number.parseInt(event.target.value, 10);
                      if (CAPTURE_BIT_DEPTH_OPTIONS.includes(parsed)) {
                        setCaptureBitDepth(parsed);
                      }
                    }}
                    bg={inputBg}
                  >
                    {CAPTURE_BIT_DEPTH_OPTIONS.map((depth) => (
                      <option key={depth} value={depth.toString()}>{depth}-bit</option>
                    ))}
                  </Select>
                </FormControl>
              </SimpleGrid>
            </>
          )}
          
          <FormControl display="flex" alignItems="center">
            <FormLabel htmlFor="enabled" mb="0">
              Enabled
            </FormLabel>
            <Switch 
              id="enabled" 
              isChecked={enabled}
              onChange={(e) => setEnabled(e.target.checked)}
            />
          </FormControl>
          
          <FormControl>
            <FormLabel>Volume</FormLabel>
            <VolumeSlider
              value={volume}
              onChange={setVolume}
              dataTutorialId="source-volume-slider"
            />
          </FormControl>

          <FormControl>
            <FormLabel>Delay (ms)</FormLabel>
            <NumberInput
              data-tutorial-id="source-delay-input"
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
              dataTutorialId="source-timeshift-slider"
            />
          </FormControl>

          <Heading as="h3" size="md" mt={4} mb={2}>
            VNC Settings (Optional)
          </Heading>
          
          <FormControl>
            <FormLabel>VNC IP</FormLabel>
            <Input
              data-tutorial-id="source-vnc-ip-input"
              value={vncIp}
              onChange={(e) => setVncIp(e.target.value)}
              bg={inputBg}
              placeholder="Leave empty if not using VNC"
            />
          </FormControl>

          <FormControl>
            <FormLabel>VNC Port</FormLabel>
            <NumberInput
              data-tutorial-id="source-vnc-port-input"
              value={vncPort}
              onChange={(valueString) => setVncPort(valueString)}
              min={1}
              max={65535}
              bg={inputBg}
              isDisabled={!vncIp}
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
            data-tutorial-id="source-submit-button"
          >
            {isEdit ? 'Update Source' : 'Add Source'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditSourcePage;

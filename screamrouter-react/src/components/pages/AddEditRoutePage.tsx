/**
 * React component for adding or editing a route in a standalone page.
 * This component provides a form to input details about a route, including its name, source, sink,
 * volume, delay, and timeshift settings.
 * It allows the user to either add a new route or update an existing one.
 */
import React, { useState, useEffect, useCallback, useMemo } from 'react';
import axios from 'axios';
import { useSearchParams } from 'react-router-dom';
import {
  Flex,
  FormControl,
  FormLabel,
  Input,
  Select,
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
  Heading,
  Container,
  useColorModeValue,
  Switch,
  Text,
  Spinner,
  SimpleGrid
} from '@chakra-ui/react';
import ApiService, { Route, Source, Sink, Equalizer } from '../../api/api';
import { useTutorial } from '../../context/TutorialContext';
import VolumeSlider from './controls/VolumeSlider';
import TimeshiftSlider from './controls/TimeshiftSlider';
import { useRouterInstances, RouterInstance } from '../../hooks/useRouterInstances';

const flatEqualizer: Equalizer = {
  b1: 1, b2: 1, b3: 1, b4: 1, b5: 1, b6: 1,
  b7: 1, b8: 1, b9: 1, b10: 1, b11: 1, b12: 1,
  b13: 1, b14: 1, b15: 1, b16: 1, b17: 1, b18: 1,
  normalization_enabled: false,
};

const DEFAULT_REMOTE_RTP_PORT = 40000;

const AddEditRoutePage: React.FC = () => {
  const [searchParams] = useSearchParams();
  const routeName = searchParams.get('name');
  const preselectedSource = searchParams.get('source');
  const preselectedSink = searchParams.get('sink');
  const isEdit = !!routeName;

  const { completeStep, nextStep } = useTutorial();

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [route, setRoute] = useState<Route | null>(null);
  const [configId, setConfigId] = useState('');
  const [routeTag, setRouteTag] = useState('');
  const [name, setName] = useState('');
  const [nameManuallyEdited, setNameManuallyEdited] = useState(false);
  const [source, setSource] = useState('');
  const [sink, setSink] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [volume, setVolume] = useState(1);
  const [delay, setDelay] = useState(0);
  const [timeshift, setTimeshift] = useState(0);
  
  const [sources, setSources] = useState<Source[]>([]);
  const [sinks, setSinks] = useState<Sink[]>([]);
  const [remoteSinks, setRemoteSinks] = useState<Sink[]>([]);
  const [selectedServerId, setSelectedServerId] = useState('local');
  const [remoteSinkSelection, setRemoteSinkSelection] = useState('');
  const [remoteLoading, setRemoteLoading] = useState(false);
  const [remoteError, setRemoteError] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);
  const { instances, loading: instancesLoading, error: instancesError, refresh: refreshInstances } = useRouterInstances();
  const selectedInstance = useMemo<RouterInstance | null>(
    () => instances.find(instance => instance.id === selectedServerId) || null,
    [instances, selectedServerId]
  );

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  const resolveApiBase = useCallback((instance: RouterInstance) => {
    const apiPath = "/";//(instance.properties?.api || '').trim();
    if (!apiPath) {
      return instance.origin;
    }
    const normalized = apiPath.startsWith('/') ? apiPath : `/${apiPath}`;
    return `${instance.origin}${normalized}`;
  }, []);

  const refreshLocalSinks = useCallback(async () => {
    try {
      const response = await ApiService.getSinks();
      setSinks(Object.values(response.data));
    } catch (err) {
      console.error('Failed to refresh sinks', err);
    }
  }, []);

  const fetchRemoteResources = useCallback(async (instance: RouterInstance) => {
    setRemoteLoading(true);
    setRemoteError(null);
    try {
      const baseURL = resolveApiBase(instance).replace(/\/$/, '');
      const [, sinksResponse] = await Promise.all([
        Promise.resolve(),
        axios.get<Record<string, Sink>>(`${baseURL}/sinks`)
      ]);
      setRemoteSinks(Object.values(sinksResponse.data || {}));
    } catch (err) {
      console.error('Failed to fetch remote sinks', err);
      setRemoteError('Failed to load remote sinks.');
      setRemoteSinks([]);
    } finally {
      setRemoteLoading(false);
    }
  }, [resolveApiBase]);

  const ensureUniqueName = useCallback((base: string, existingNames: string[]) => {
    let candidate = base;
    let counter = 1;
    while (existingNames.includes(candidate)) {
      candidate = `${base} (${counter})`;
      counter += 1;
    }
    return candidate;
  }, []);

  const findExistingLocalSink = useCallback((remoteSink: Sink, instance: RouterInstance | null) => {
    if (!instance) {
      return null;
    }
    const candidates = [instance.hostname, instance.address, instance.uuid].filter(Boolean);
    const match = sinks.find((localSink) => {
      const sinkMatch = localSink.sap_target_sink === remoteSink.config_id || localSink.sap_target_sink === remoteSink.name;
      if (!sinkMatch) {
        return false;
      }
      if (!localSink.sap_target_host) {
        return true;
      }
      return candidates.includes(localSink.sap_target_host);
    });
    return match ? match.name : null;
  }, [sinks]);

  const ensureRemoteSinkMapping = useCallback(async (remoteSink: Sink): Promise<string | null> => {
    if (!selectedInstance) {
      setError('Select a server first.');
      return null;
    }
    const targetHost = selectedInstance.uuid || selectedInstance.hostname || selectedInstance.address || '';
    const matching = findExistingLocalSink(remoteSink, selectedInstance);
    if (matching) {
      setSelectedServerId('local');
      setRemoteSinkSelection('');
      setSink(matching);
      setSuccess(`Using existing mapped sink "${matching}" for remote sink "${remoteSink.name}".`);
      return matching;
    }

    const baseName = `${selectedInstance.hostname || selectedInstance.label || 'Remote'}-${remoteSink.name}`;
    const sinkName = ensureUniqueName(baseName, sinks.map(s => s.name));
    const destinationHost = selectedInstance.address || selectedInstance.hostname || remoteSink.ip || '';
    const randomizedPort = Math.floor(Math.random() * 9000) + 41000; // 41000-49999

    const sinkPayload: Sink = {
      ...remoteSink,
      name: sinkName,
      ip: destinationHost,
      port: randomizedPort || DEFAULT_REMOTE_RTP_PORT,
      enabled: true,
      is_group: false,
      group_members: [],
      volume: remoteSink.volume ?? 1,
      equalizer: remoteSink.equalizer || flatEqualizer,
      bit_depth: remoteSink.bit_depth || 16,
      sample_rate: remoteSink.sample_rate || 48000,
      channels: remoteSink.channels || 2,
      channel_layout: remoteSink.channel_layout || 'stereo',
      delay: remoteSink.delay ?? 0,
      timeshift: remoteSink.timeshift ?? 0,
      time_sync: remoteSink.time_sync ?? false,
      time_sync_delay: remoteSink.time_sync_delay ?? 0,
      speaker_layouts: remoteSink.speaker_layouts || {},
      protocol: 'rtp',
      volume_normalization: remoteSink.volume_normalization ?? false,
      multi_device_mode: false,
      rtp_receiver_mappings: [],
      sap_target_sink: remoteSink.config_id || remoteSink.name,
      sap_target_host: targetHost,
    };

    try {
      setError(null);
      await ApiService.addSink(sinkPayload);
      await refreshLocalSinks();
      setSelectedServerId('local');
      setRemoteSinkSelection('');
      setSuccess(`Created sink "${sinkName}" targeting ${remoteSink.name} on ${selectedInstance.label || selectedInstance.hostname}.`);
      return sinkName;
    } catch (err) {
      console.error('Failed to create remote sink mapping', err);
      setError('Failed to create a local sink for the remote target.');
      return null;
    }
  }, [ensureUniqueName, refreshLocalSinks, selectedInstance, sinks]);

  const handleSinkChange = useCallback(async (value: string) => {
    if (selectedServerId === 'local') {
      setSink(value);
      setRemoteSinkSelection('');
      return;
    }
    setRemoteSinkSelection(value);
    const remoteSink = remoteSinks.find(s => s.name === value);
    if (!remoteSink || !selectedInstance) {
      setError('Select a remote server and sink first.');
      return;
    }
    const existing = findExistingLocalSink(remoteSink, selectedInstance);
    if (existing) {
      setSink(existing);
      setSelectedServerId('local');
      setRemoteSinkSelection('');
      setSuccess(`Using existing mapped sink "${existing}" for remote sink "${remoteSink.name}".`);
      return;
    }
    const mappedName = await ensureRemoteSinkMapping(remoteSink);
    if (mappedName) {
      setSink(mappedName);
      setSelectedServerId('local');
      setRemoteSinkSelection('');
    }
  }, [ensureRemoteSinkMapping, findExistingLocalSink, remoteSinks, selectedInstance, selectedServerId]);

  useEffect(() => {
    if (!name.trim()) {
      return;
    }
    completeStep('route-name-input');
  }, [name, completeStep]);

  useEffect(() => {
    if (!source) {
      return;
    }
    completeStep('route-source-select');
  }, [source, completeStep]);

  useEffect(() => {
    if (!sink) {
      return;
    }
    completeStep('route-sink-select');
  }, [sink, completeStep]);

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
            form: 'route',
          },
          targetOrigin
        );
      } catch (error) {
        console.error('Failed to announce route form closing', error);
      }
    };

    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => {
      window.removeEventListener('beforeunload', handleBeforeUnload);
    };
  }, []);

  // Fetch sources and sinks
  useEffect(() => {
    const fetchData = async () => {
      try {
          const [sourcesResponse, sinksResponse] = await Promise.all([
            ApiService.getSources(),
            ApiService.getSinks()
          ]);
        
        setSources(Object.values(sourcesResponse.data));
          setSinks(Object.values(sinksResponse.data));
        
        // Set preselected source and sink if provided in URL
        if (preselectedSource && !isEdit) {
          const sourceExists = Object.values(sourcesResponse.data).some(
            s => s.name === preselectedSource
          );
          if (sourceExists) {
            setSource(preselectedSource);
          }
        }
        
        if (preselectedSink && !isEdit) {
          const sinkExists = Object.values(sinksResponse.data).some(
            s => s.name === preselectedSink
          );
          if (sinkExists) {
            setSink(preselectedSink);
          }
        }
      } catch (error) {
        console.error('Error fetching sources and sinks:', error);
        setError('Failed to fetch sources and sinks. Please try again.');
      }
    };

    fetchData();
  }, [preselectedSource, preselectedSink, isEdit]);

  // Fetch route data if editing
  useEffect(() => {
    const fetchRoute = async () => {
      if (routeName) {
        try {
          const response = await ApiService.getRoutes();
          const routeData = Object.values(response.data).find(r => r.name === routeName);
          if (routeData) {
            setRoute(routeData);
            setName(routeData.name);
            setSource(routeData.source);
            setSink(routeData.sink);
            setEnabled(routeData.enabled);
            setVolume(routeData.volume || 1);
            setDelay(routeData.delay || 0);
            setTimeshift(routeData.timeshift || 0);
            setConfigId((routeData as any).config_id || '');
            setRouteTag((routeData as any).tag || '');
          } else {
            setError(`Route "${routeName}" not found.`);
          }
        } catch (error) {
          console.error('Error fetching route:', error);
          setError('Failed to fetch route data. Please try again.');
        }
      }
    };

    fetchRoute();
  }, [routeName]);
  
  // Auto-fill name when source and sink are selected
  useEffect(() => {
    // Only auto-fill name if we're not in edit mode and the name hasn't been manually edited
    if (!isEdit && !nameManuallyEdited && source && sink) {
      setName(`${source} to ${sink}`);
    }
  }, [source, sink, nameManuallyEdited, isEdit]);

  /**
   * Handles form submission to add or update a route.
   * Validates input and sends the data to the API service.
   */
  const handleSubmit = async () => {
    if (!source) {
      setError('Please select a source.');
      return;
    }

    if (!sink) {
      setError('Please select a sink.');
      return;
    }

    const routeData: Partial<Route> = {
      name,
      source,
      sink,
      enabled,
      volume,
      delay,
      timeshift
    };

    try {
      if (isEdit) {
        await ApiService.updateRoute(routeName!, routeData);
        setSuccess(`Route "${name}" updated successfully.`);
      } else {
        await ApiService.addRoute(routeData as Route);
        setSuccess(`Route "${name}" added successfully.`);
      }

      completeStep('route-submit');
      nextStep();

      try {
        const targetOrigin = window.location.origin;
        window.opener?.postMessage(
          {
            type: 'RESOURCE_ADDED',
            resourceType: 'route',
            action: isEdit ? 'updated' : 'added',
            name,
          },
          targetOrigin
        );
      } catch (messageError) {
        console.error('Failed to post resource update message', messageError);
      }

      // Clear form if adding a new route
      if (!isEdit) {
        setName('');
        setSource('');
        setSink('');
        setEnabled(true);
        setVolume(1);
        setDelay(0);
        setTimeshift(0);
        setConfigId('');
        setRouteTag('');
      }

      if (window.opener) {
        setTimeout(() => {
          try {
            window.close();
          } catch (closeError) {
            console.error('Failed to close window after route submission', closeError);
          }
        }, 200);
      }
    } catch (error) {
      console.error('Error submitting route:', error);
      setError('Failed to submit route. Please try again.');
    }
  };

  const handleClose = () => {
    window.close();
  };

  return (
    <Container maxW="container.lg" py={8}>
      <Box
        bg={bgColor}
        borderColor={borderColor}
        borderWidth="1px"
        borderRadius="lg"
        p={6}
        boxShadow="md"
      >
        <Heading as="h1" size="lg" mb={6}>
          {isEdit ? 'Edit Route' : 'Add Route'}
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
        
        {(configId || routeTag) && (
          <Box
            mb={5}
            p={4}
            borderWidth="1px"
            borderRadius="md"
            bg={useColorModeValue('gray.50', 'gray.700')}
            borderColor={useColorModeValue('gray.200', 'gray.600')}
          >
            <SimpleGrid columns={{ base: 1, md: 2 }} spacing={3}>
              {configId && (
                <Box>
                  <Text fontSize="xs" color={useColorModeValue('gray.500', 'gray.400')} textTransform="uppercase" letterSpacing="0.05em">
                    GUID
                  </Text>
                  <Text fontWeight="semibold" fontSize="sm">{configId}</Text>
                </Box>
              )}
              {routeTag && (
                <Box>
                  <Text fontSize="xs" color={useColorModeValue('gray.500', 'gray.400')} textTransform="uppercase" letterSpacing="0.05em">
                    Tag
                  </Text>
                  <Text fontWeight="semibold" fontSize="sm">{routeTag}</Text>
                </Box>
              )}
            </SimpleGrid>
          </Box>
        )}

        <Stack spacing={5}>
          <SimpleGrid columns={{ base: 1, md: 2 }} spacing={4}>
            <FormControl isRequired>
              <FormLabel>Route Name</FormLabel>
              <Input
                data-tutorial-id="route-name-input"
                value={name}
                onChange={(e) => {
                  setName(e.target.value);
                  setNameManuallyEdited(e.target.value !== "");
                }}
                bg={inputBg}
              />
            </FormControl>

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
          </SimpleGrid>

          <SimpleGrid columns={{ base: 1, md: 2 }} spacing={4}>
            <FormControl>
              <FormLabel>Volume</FormLabel>
              <VolumeSlider
                value={volume}
                onChange={setVolume}
                dataTutorialId="route-volume-slider"
              />
            </FormControl>

            <FormControl>
              <FormLabel>Timeshift</FormLabel>
              <TimeshiftSlider
                value={timeshift}
                onChange={setTimeshift}
                dataTutorialId="route-timeshift-slider"
              />
            </FormControl>
          </SimpleGrid>

          <SimpleGrid columns={{ base: 1, md: 2 }} spacing={4}>
            <FormControl isRequired>
              <FormLabel>Source</FormLabel>
              <Select
                data-tutorial-id="route-source-select"
                value={source}
                onChange={(e) => setSource(e.target.value)}
                bg={inputBg}
                placeholder="Select a source"
              >
                {sources.map(src => (
                  <option key={src.name} value={src.name}>
                    {src.name}
                  </option>
                ))}
              </Select>
            </FormControl>

            <FormControl isRequired>
              <FormLabel>Sink ScreamRouter Server</FormLabel>
              <Select
                value={selectedServerId}
                onChange={(e) => {
                  const nextId = e.target.value;
                  setSelectedServerId(nextId);
                  setRemoteSinkSelection('');
                  if (nextId === 'local') {
                    setRemoteSinks([]);
                    setRemoteError(null);
                    setRemoteLoading(false);
                    setRemoteSinkSelection('');
                  } else {
                    const instance = instances.find(inst => inst.id === nextId);
                    if (instance) {
                      void fetchRemoteResources(instance);
                    }
                  }
                }}
              >
                <option value="local">Local</option>
                {instances.filter(instance => !instance.isCurrent).map(instance => (
                  <option key={instance.id} value={instance.id}>
                    {instance.label} {instance.isCurrent ? '(current)' : ''}
                  </option>
                ))}
              </Select>
              {selectedServerId !== 'local' && (
                <Text fontSize="xs" color="gray.500" mt={1}>
                  Remote sink selection will create a local RTP sender tagged for the target router.
                </Text>
              )}
            </FormControl>
          </SimpleGrid>

          <FormControl isRequired>
            <FormLabel>Sink</FormLabel>
            <Select
              data-tutorial-id="route-sink-select"
              value={selectedServerId === 'local' ? sink : remoteSinkSelection}
              onChange={(e) => { void handleSinkChange(e.target.value); }}
              bg={inputBg}
              placeholder={selectedServerId === 'local' ? 'Select a sink' : (remoteLoading ? 'Loading remote sinks...' : 'Select a remote sink')}
              isDisabled={selectedServerId !== 'local' && (remoteLoading || Boolean(remoteError))}
            >
              {(selectedServerId === 'local' ? sinks : remoteSinks).map(s => (
                <option key={s.name} value={s.name}>
                  {s.name}
                </option>
              ))}
            </Select>
            {remoteError && (
              <Alert status="error" mt={2} borderRadius="md">
                <AlertIcon />
                {remoteError}
              </Alert>
            )}
            {instancesError && selectedServerId !== 'local' && (
              <Alert status="error" mt={2} borderRadius="md">
                <AlertIcon />
                {instancesError}
              </Alert>
            )}
            {remoteLoading && selectedServerId !== 'local' && (
              <Flex align="center" gap={2} mt={2}>
                <Spinner size="sm" />
                <Text fontSize="sm">Loading remote sinks...</Text>
              </Flex>
            )}
          </FormControl>

          <FormControl>
            <FormLabel>Delay (ms)</FormLabel>
            <NumberInput
              data-tutorial-id="route-delay-input"
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
        </Stack>
        
        <Flex mt={8} gap={3} justifyContent="flex-end">
          <Button
            colorScheme="blue"
            onClick={handleSubmit}
            data-tutorial-id="route-submit-button"
          >
            {isEdit ? 'Update Route' : 'Add Route'}
          </Button>
          <Button variant="outline" onClick={handleClose}>
            Close
          </Button>
        </Flex>
      </Box>
    </Container>
  );
};

export default AddEditRoutePage;

/**
 * React component for adding or editing a route in a standalone page.
 * This component provides a form to input details about a route, including its name, source, sink,
 * volume, delay, and timeshift settings.
 * It allows the user to either add a new route or update an existing one.
 */
import React, { useState, useEffect, useCallback, useMemo, useRef } from 'react';
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
import ApiService, { Route, Source, Sink, Equalizer, NeighborSinksRequest } from '../../api/api';
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
const REMOTE_UNAVAILABLE_MESSAGE = 'Remote server/sink unavailable (not found).';
const REMOTE_SOURCE_UNAVAILABLE_MESSAGE = 'Remote server/source unavailable (not found).';

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
  const [remoteSources, setRemoteSources] = useState<Source[]>([]);
  const [sourceMode, setSourceMode] = useState<'local' | 'remote'>('local');
  const [destinationMode, setDestinationMode] = useState<'local' | 'remote'>('local');
  const [remoteInstanceId, setRemoteInstanceId] = useState('');
  const [remoteTarget, setRemoteTarget] = useState<Route['remote_target'] | null>(null);
  const [remoteSinkSelection, setRemoteSinkSelection] = useState('');
  const [remoteSourceInstanceId, setRemoteSourceInstanceId] = useState('');
  const [remoteSourceRequest, setRemoteSourceRequest] = useState<Route['remote_source_request'] | null>(null);
  const [remoteSourceSelection, setRemoteSourceSelection] = useState('');
  const [remoteLoading, setRemoteLoading] = useState(false);
  const [remoteSourceLoading, setRemoteSourceLoading] = useState(false);
  const [remoteError, setRemoteError] = useState<string | null>(null);
  const [remoteSourceError, setRemoteSourceError] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState<string | null>(null);
  const { instances, loading: instancesLoading, error: instancesError, refresh: refreshInstances } = useRouterInstances();
  const selectedRemoteInstance = useMemo<RouterInstance | null>(
    () => instances.find(instance => instance.id === remoteInstanceId) || null,
    [instances, remoteInstanceId]
  );
  const selectedRemoteSourceInstance = useMemo<RouterInstance | null>(
    () => instances.find(instance => instance.id === remoteSourceInstanceId) || null,
    [instances, remoteSourceInstanceId]
  );
  const remoteFetchKeyRef = useRef<string | null>(null);
  const remoteSourceFetchKeyRef = useRef<string | null>(null);

  // Color values for light/dark mode
  const bgColor = useColorModeValue('white', 'gray.800');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const inputBg = useColorModeValue('white', 'gray.700');

  const normalizeHostValue = useCallback((value?: string | null): string => {
    if (!value) {
      return '';
    }
    return value.trim().replace(/\.$/, '').toLowerCase();
  }, []);

  const buildNeighborPayload = useCallback((instance: RouterInstance): NeighborSinksRequest => {
    const normalizedScheme = instance.scheme?.toLowerCase() === 'http' ? 'http' : 'https';
    const fallbackHost = instance.address || instance.hostname;
    return {
      hostname: instance.hostname || fallbackHost,
      address: instance.address || undefined,
      port: instance.port,
      scheme: normalizedScheme,
      api_path: '/',
      verify_tls: false,
    };
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
      const response = await ApiService.getNeighborSinks(buildNeighborPayload(instance));
      const payload = response.data;
      const sinksList = Array.isArray(payload) ? payload : Object.values(payload || {});
      setRemoteSinks(sinksList);
    } catch (err) {
      console.error('Failed to fetch remote sinks', err);
      setRemoteError(REMOTE_UNAVAILABLE_MESSAGE);
      setRemoteSinks([]);
    } finally {
      setRemoteLoading(false);
    }
  }, [buildNeighborPayload]);

  const fetchRemoteSources = useCallback(async (instance: RouterInstance) => {
    setRemoteSourceLoading(true);
    setRemoteSourceError(null);
    try {
      const response = await ApiService.getNeighborSources(buildNeighborPayload(instance));
      const payload = response.data;
      const sourcesList = Array.isArray(payload) ? payload : Object.values(payload || {});
      setRemoteSources(sourcesList);
    } catch (err) {
      console.error('Failed to fetch remote sources', err);
      setRemoteSourceError(REMOTE_SOURCE_UNAVAILABLE_MESSAGE);
      setRemoteSources([]);
    } finally {
      setRemoteSourceLoading(false);
    }
  }, [buildNeighborPayload]);

  const buildPayloadFromEndpoint = useCallback((
    endpoint: Route['remote_target'] | Route['remote_source_request'] | null
  ): NeighborSinksRequest | null => {
    if (!endpoint) {
      return null;
    }
    const host = endpoint.router_hostname || endpoint.router_address;
    if (!host) {
      return null;
    }
    const scheme = endpoint.router_scheme === 'http' ? 'http' : 'https';
    const port = endpoint.router_port || (scheme === 'http' ? 80 : 443);
    return {
      hostname: host,
      address: endpoint.router_address || undefined,
      port,
      scheme,
      api_path: '/',
      verify_tls: false,
    };
  }, []);

  const fetchRemoteResourcesFromTarget = useCallback(async (target: Route['remote_target']) => {
    const payload = buildPayloadFromEndpoint(target);
    if (!payload) {
      setRemoteError(REMOTE_UNAVAILABLE_MESSAGE);
      setRemoteSinks([]);
      return;
    }
    setRemoteLoading(true);
    setRemoteError(null);
    try {
      const response = await ApiService.getNeighborSinks(payload);
      const data = response.data;
      const sinksList = Array.isArray(data) ? data : Object.values(data || {});
      setRemoteSinks(sinksList);
    } catch (err) {
      console.error('Failed to fetch remote sinks', err);
      setRemoteError(REMOTE_UNAVAILABLE_MESSAGE);
      setRemoteSinks([]);
    } finally {
      setRemoteLoading(false);
    }
  }, [buildPayloadFromEndpoint]);

  const fetchRemoteSourcesFromRequest = useCallback(async (request: Route['remote_source_request']) => {
    const payload = buildPayloadFromEndpoint(request);
    if (!payload) {
      setRemoteSourceError(REMOTE_SOURCE_UNAVAILABLE_MESSAGE);
      setRemoteSources([]);
      return;
    }
    setRemoteSourceLoading(true);
    setRemoteSourceError(null);
    try {
      const response = await ApiService.getNeighborSources(payload);
      const data = response.data;
      const sourcesList = Array.isArray(data) ? data : Object.values(data || {});
      setRemoteSources(sourcesList);
    } catch (err) {
      console.error('Failed to fetch remote sources', err);
      setRemoteSourceError(REMOTE_SOURCE_UNAVAILABLE_MESSAGE);
      setRemoteSources([]);
    } finally {
      setRemoteSourceLoading(false);
    }
  }, [buildPayloadFromEndpoint]);

  const findInstanceForEndpoint = useCallback((
    target: Route['remote_target'] | Route['remote_source_request'] | null
  ): RouterInstance | null => {
    if (!target) {
      return null;
    }
    const targetUuid = target.router_uuid?.trim();
    const targetHost = normalizeHostValue(target.router_hostname);
    const targetAddr = normalizeHostValue(target.router_address);
    const targetPort = target.router_port || (target.router_scheme === 'http' ? 80 : 443);

    return (
      instances.find(instance => {
        if (targetUuid && instance.uuid === targetUuid) {
          return true;
        }
        const instanceHost = normalizeHostValue(instance.hostname);
        const instanceAddr = normalizeHostValue(instance.address);
        if (targetHost && instanceHost === targetHost && instance.port === targetPort) {
          return true;
        }
        if (targetAddr && instanceAddr === targetAddr && instance.port === targetPort) {
          return true;
        }
        return false;
      }) || null
    );
  }, [instances, normalizeHostValue]);

  const ensureUniqueName = useCallback((base: string, existingNames: string[]) => {
    let candidate = base;
    let counter = 1;
    while (existingNames.includes(candidate)) {
      candidate = `${base} (${counter})`;
      counter += 1;
    }
    return candidate;
  }, []);

  const handleSinkChange = useCallback((value: string) => {
    setSink(value);
    setRemoteTarget(null);
    setRemoteSinkSelection('');
  }, []);

  const handleRemoteInstanceChange = useCallback((value: string) => {
    setRemoteInstanceId(value);
    setRemoteSinkSelection('');
    setRemoteTarget(null);
    setRemoteError(null);
    setRemoteSinks([]);
    if (!value) {
      return;
    }
    const instance = instances.find(inst => inst.id === value);
    if (instance) {
      void fetchRemoteResources(instance);
    }
  }, [fetchRemoteResources, instances]);

  const handleRemoteSinkSelection = useCallback((value: string) => {
    setRemoteSinkSelection(value);
    if (!value) {
      setRemoteTarget(null);
      return;
    }
    const instance = selectedRemoteInstance;
    const remoteSink = remoteSinks.find(s => s.name === value);
    if (!instance || !remoteSink) {
      setError('Select a remote server and sink first.');
      return;
    }
    const baseName = `${remoteSink.name} @ ${instance.label || instance.hostname || instance.address || 'Remote'}`;
    const sinkName = ensureUniqueName(baseName, sinks.map(s => s.name));
    setSink(sinkName);
    setRemoteTarget({
      router_uuid: instance.uuid,
      router_hostname: instance.hostname,
      router_address: instance.address,
      router_port: instance.port,
      router_scheme: instance.scheme === 'http' ? 'http' : 'https',
      sink_config_id: remoteSink.config_id,
      sink_name: remoteSink.name,
    });
    setSuccess(`Route will target remote sink "${remoteSink.name}" on ${instance.label || instance.hostname}.`);
  }, [ensureUniqueName, remoteSinks, selectedRemoteInstance, sinks]);

  const handleRemoteSourceInstanceChange = useCallback((value: string) => {
    setRemoteSourceInstanceId(value);
    setRemoteSourceSelection('');
    setRemoteSourceRequest(null);
    setRemoteSourceError(null);
    setRemoteSources([]);
    if (!value) {
      return;
    }
    const instance = instances.find(inst => inst.id === value);
    if (instance) {
      void fetchRemoteSources(instance);
    }
  }, [fetchRemoteSources, instances]);

  const handleRemoteSourceSelection = useCallback((value: string) => {
    setRemoteSourceSelection(value);
    if (!value) {
      setRemoteSourceRequest(null);
      return;
    }
    const instance = selectedRemoteSourceInstance;
    const remoteSource = remoteSources.find(s => s.name === value);
    if (!instance || !remoteSource) {
      setError('Select a remote server and source first.');
      return;
    }
    const baseName = `${remoteSource.name} @ ${instance.label || instance.hostname || instance.address || 'Remote'}`;
    const sourceName = ensureUniqueName(baseName, sources.map(s => s.name));
    setSource(sourceName);
    setRemoteSourceRequest({
      router_uuid: instance.uuid,
      router_hostname: instance.hostname,
      router_address: instance.address,
      router_port: instance.port,
      router_scheme: instance.scheme === 'http' ? 'http' : 'https',
      source_config_id: remoteSource.config_id,
      source_name: remoteSource.name,
    });
    setSuccess(`Route will request remote source "${remoteSource.name}" from ${instance.label || instance.hostname}.`);
  }, [ensureUniqueName, remoteSources, selectedRemoteSourceInstance, sources]);

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
    if (destinationMode === 'local') {
      setRemoteInstanceId('');
      setRemoteTarget(null);
      setRemoteSinkSelection('');
      setRemoteSinks([]);
      setRemoteError(null);
      setRemoteLoading(false);
    }
  }, [destinationMode]);

  useEffect(() => {
    if (sourceMode === 'remote' && destinationMode === 'remote') {
      //setDestinationMode('local');
    }
  }, [sourceMode, destinationMode]);

  useEffect(() => {
    if (sourceMode === 'local') {
      setRemoteSourceInstanceId('');
      setRemoteSourceRequest(null);
      setRemoteSourceSelection('');
      setRemoteSources([]);
      setRemoteSourceError(null);
      setRemoteSourceLoading(false);
    }
  }, [sourceMode]);

  useEffect(() => {
    if (remoteTarget?.sink_name) {
      setRemoteSinkSelection(remoteTarget.sink_name);
    }
  }, [remoteTarget]);

  useEffect(() => {
    if (remoteSourceRequest?.source_name) {
      setRemoteSourceSelection(remoteSourceRequest.source_name);
    }
  }, [remoteSourceRequest]);

  useEffect(() => {
    if (!remoteTarget) {
      return;
    }
    if (!remoteSinks.length) {
      return;
    }
    const matchByConfig = remoteTarget.sink_config_id
      ? remoteSinks.find(s => s.config_id === remoteTarget.sink_config_id)
      : undefined;
    const matchByName = remoteSinks.find(s => s.name === remoteTarget.sink_name);
    const resolved = matchByConfig || matchByName;
    if (resolved) {
      setRemoteSinkSelection(resolved.name);
      setRemoteError(null);
    } else if (!remoteLoading) {
      setRemoteError(REMOTE_UNAVAILABLE_MESSAGE);
    }
  }, [remoteTarget, remoteSinks, remoteLoading]);

  useEffect(() => {
    if (!remoteSourceRequest) {
      return;
    }
    if (!remoteSources.length) {
      return;
    }
    const matchByConfig = remoteSourceRequest.source_config_id
      ? remoteSources.find(s => s.config_id === remoteSourceRequest.source_config_id)
      : undefined;
    const matchByName = remoteSources.find(s => s.name === remoteSourceRequest.source_name);
    const resolved = matchByConfig || matchByName;
    if (resolved) {
      setRemoteSourceSelection(resolved.name);
      setRemoteSourceError(null);
    } else if (!remoteSourceLoading) {
      setRemoteSourceError(REMOTE_SOURCE_UNAVAILABLE_MESSAGE);
    }
  }, [remoteSourceRequest, remoteSources, remoteSourceLoading]);

  useEffect(() => {
    if (!remoteTarget) {
      return;
    }
    const matchingInstance = findInstanceForEndpoint(remoteTarget);
    if (matchingInstance) {
      if (matchingInstance.id !== remoteInstanceId) {
        setRemoteInstanceId(matchingInstance.id);
      }
    } else if (remoteInstanceId) {
      setRemoteInstanceId('');
    }
  }, [findInstanceForEndpoint, remoteInstanceId, remoteTarget]);

  useEffect(() => {
    if (!remoteSourceRequest) {
      return;
    }
    const matchingInstance = findInstanceForEndpoint(remoteSourceRequest);
    if (matchingInstance) {
      if (matchingInstance.id !== remoteSourceInstanceId) {
        setRemoteSourceInstanceId(matchingInstance.id);
      }
    } else if (remoteSourceInstanceId) {
      setRemoteSourceInstanceId('');
    }
  }, [findInstanceForEndpoint, remoteSourceInstanceId, remoteSourceRequest]);

  useEffect(() => {
    if (destinationMode !== 'remote') {
      remoteFetchKeyRef.current = null;
      return;
    }
    if (remoteInstanceId) {
      const instance = instances.find(inst => inst.id === remoteInstanceId);
      if (instance) {
        const key = `instance:${remoteInstanceId}`;
        if (remoteFetchKeyRef.current !== key) {
          remoteFetchKeyRef.current = key;
          void fetchRemoteResources(instance);
        }
        return;
      }
    }
    if (remoteTarget) {
      const hostKey = `${normalizeHostValue(remoteTarget.router_hostname || remoteTarget.router_address)}:${remoteTarget.router_port || (remoteTarget.router_scheme === 'http' ? 80 : 443)}`;
      const key = `target:${hostKey}`;
      if (remoteFetchKeyRef.current !== key) {
        remoteFetchKeyRef.current = key;
        void fetchRemoteResourcesFromTarget(remoteTarget);
      }
      return;
    }
    remoteFetchKeyRef.current = null;
  }, [
    destinationMode,
    remoteInstanceId,
    remoteTarget,
    instances,
    fetchRemoteResources,
    fetchRemoteResourcesFromTarget,
    normalizeHostValue,
  ]);

  useEffect(() => {
    if (sourceMode !== 'remote') {
      remoteSourceFetchKeyRef.current = null;
      return;
    }
    if (remoteSourceInstanceId) {
      const instance = instances.find(inst => inst.id === remoteSourceInstanceId);
      if (instance) {
        const key = `source-instance:${remoteSourceInstanceId}`;
        if (remoteSourceFetchKeyRef.current !== key) {
          remoteSourceFetchKeyRef.current = key;
          void fetchRemoteSources(instance);
        }
        return;
      }
    }
    if (remoteSourceRequest) {
      const hostKey = `${normalizeHostValue(remoteSourceRequest.router_hostname || remoteSourceRequest.router_address)}:${remoteSourceRequest.router_port || (remoteSourceRequest.router_scheme === 'http' ? 80 : 443)}`;
      const key = `source-target:${hostKey}`;
      if (remoteSourceFetchKeyRef.current !== key) {
        remoteSourceFetchKeyRef.current = key;
        void fetchRemoteSourcesFromRequest(remoteSourceRequest);
      }
      return;
    }
    remoteSourceFetchKeyRef.current = null;
  }, [
    sourceMode,
    remoteSourceInstanceId,
    remoteSourceRequest,
    instances,
    fetchRemoteSources,
    fetchRemoteSourcesFromRequest,
    normalizeHostValue,
  ]);

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
            if ((routeData as Route).remote_source_request) {
              setSourceMode('remote');
              setRemoteSourceRequest(routeData.remote_source_request || null);
              setRemoteSourceSelection(routeData.remote_source_request?.source_name || routeData.remote_source_request?.source_config_id || '');
            } else {
              setSourceMode('local');
              setRemoteSourceRequest(null);
              setRemoteSourceSelection('');
            }
            if ((routeData as Route).remote_target) {
              setDestinationMode('remote');
              setRemoteTarget(routeData.remote_target || null);
              setRemoteSinkSelection(routeData.remote_target?.sink_name || routeData.remote_target?.sink_config_id || '');
            } else {
              setDestinationMode('local');
              setRemoteTarget(null);
              setRemoteSinkSelection('');
            }
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

    if (sourceMode === 'remote' && !remoteSourceRequest) {
      setError('Please select a remote source.');
      return;
    }

    if (!sink) {
      setError('Please select a sink.');
      return;
    }

    if (destinationMode === 'remote' && !remoteTarget) {
      setError('Please select a remote sink.');
      return;
    }

    const routeData: Partial<Route> = {
      name,
      source,
      sink,
      enabled,
      volume,
      delay,
      timeshift,
      remote_target: destinationMode === 'remote' ? remoteTarget || undefined : undefined,
      remote_source_request: sourceMode === 'remote' ? remoteSourceRequest || undefined : undefined,
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
        setSourceMode('local');
        setDestinationMode('local');
        setRemoteTarget(null);
        setRemoteInstanceId('');
        setRemoteSinkSelection('');
        setRemoteSourceRequest(null);
        setRemoteSourceInstanceId('');
        setRemoteSourceSelection('');
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
            <FormControl>
              <FormLabel>Source Type</FormLabel>
              <Select
                value={sourceMode}
                onChange={(e) => setSourceMode(e.target.value as 'local' | 'remote')}
                bg={inputBg}
              >
                <option value="local">Local source</option>
                <option value="remote">Remote router source</option>
              </Select>
            </FormControl>

            <FormControl>
              <FormLabel>Destination Type</FormLabel>
              <Select
                value={destinationMode}
                onChange={(e) => setDestinationMode(e.target.value as 'local' | 'remote')}
                bg={inputBg}
              >
                <option value="local">Local sink</option>
                <option value="remote">Remote router sink</option>
              </Select>
            </FormControl>
          </SimpleGrid>

          {sourceMode === 'remote' && (
            <FormControl>
              <FormLabel>Remote Source Router</FormLabel>
              <Stack direction={{ base: 'column', md: 'row' }} spacing={2}>
                <Select
                  value={remoteSourceInstanceId}
                  onChange={(e) => handleRemoteSourceInstanceChange(e.target.value)}
                  placeholder="Select a remote router"
                  bg={inputBg}
                  flex="1"
                >
                  {instances.filter(instance => !instance.isCurrent).map(instance => (
                    <option key={instance.id} value={instance.id}>
                      ScreamRouter - {instance.label}
                    </option>
                  ))}
                </Select>
                <Button
                  size="sm"
                  onClick={() => { remoteSourceFetchKeyRef.current = null; void refreshInstances(); }}
                  isLoading={instancesLoading}
                  variant="outline"
                >
                  Refresh
                </Button>
              </Stack>
              {instancesError && (
                <Text mt={1} fontSize="sm" color="red.400">
                  {instancesError}
                </Text>
              )}
              {remoteSourceRequest && !remoteSourceInstanceId && (
                <Text mt={1} fontSize="sm" color="gray.400">
                  Stored request: {remoteSourceRequest.source_name || remoteSourceRequest.source_config_id || 'remote source'} from {remoteSourceRequest.router_hostname || remoteSourceRequest.router_address || 'remote router'}
                </Text>
              )}
            </FormControl>
          )}

          <FormControl isRequired>
            <FormLabel>{sourceMode === 'remote' ? 'Remote Source' : 'Source'}</FormLabel>
            {sourceMode === 'remote' ? (
              <>
                <Select
                  value={remoteSourceSelection}
                  onChange={(e) => handleRemoteSourceSelection(e.target.value)}
                  bg={inputBg}
                  placeholder={remoteSourceLoading ? 'Loading remote sources...' : 'Select a remote source'}
                  isDisabled={!remoteSourceInstanceId || remoteSourceLoading || Boolean(remoteSourceError)}
                >
                  {remoteSources.map(s => (
                    <option key={s.name} value={s.name}>
                      {s.name}
                    </option>
                  ))}
                </Select>
                {remoteSourceError && (
                  <Alert status="error" mt={2} borderRadius="md">
                    <AlertIcon />
                    {remoteSourceError}
                  </Alert>
                )}
                {remoteSourceLoading && (
                  <Flex align="center" gap={2} mt={2}>
                    <Spinner size="sm" />
                    <Text fontSize="sm">Loading remote sources...</Text>
                  </Flex>
                )}
              </>
            ) : (
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
            )}
          </FormControl>

          {destinationMode === 'remote' && (
            <FormControl>
              <FormLabel>Remote Router</FormLabel>
              <Stack direction={{ base: 'column', md: 'row' }} spacing={2}>
                <Select
                  value={remoteInstanceId}
                  onChange={(e) => handleRemoteInstanceChange(e.target.value)}
                  placeholder="Select a remote router"
                  bg={inputBg}
                  flex="1"
                >
                  {instances.filter(instance => !instance.isCurrent).map(instance => (
                    <option key={instance.id} value={instance.id}>
                      ScreamRouter - {instance.label}
                    </option>
                  ))}
                </Select>
                <Button
                  size="sm"
                  onClick={() => { remoteFetchKeyRef.current = null; void refreshInstances(); }}
                  isLoading={instancesLoading}
                  variant="outline"
                >
                  Refresh
                </Button>
              </Stack>
              {instancesError && (
                <Text mt={1} fontSize="sm" color="red.400">
                  {instancesError}
                </Text>
              )}
              {remoteTarget && !remoteInstanceId && (
                <Text mt={1} fontSize="sm" color="gray.400">
                  Stored target: {remoteTarget.sink_name || remoteTarget.sink_config_id || 'remote sink'} on {remoteTarget.router_hostname || remoteTarget.router_address || 'remote router'}
                </Text>
              )}
            </FormControl>
          )}

          <FormControl isRequired>
            <FormLabel>{destinationMode === 'remote' ? 'Remote Sink' : 'Sink'}</FormLabel>
            {destinationMode === 'remote' ? (
              <>
                <Select
                  value={remoteSinkSelection}
                  onChange={(e) => handleRemoteSinkSelection(e.target.value)}
                  bg={inputBg}
                  placeholder={remoteLoading ? 'Loading remote sinks...' : 'Select a remote sink'}
                  isDisabled={!remoteInstanceId || remoteLoading || Boolean(remoteError)}
                >
                  {remoteSinks.map(s => (
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
                {remoteLoading && (
                  <Flex align="center" gap={2} mt={2}>
                    <Spinner size="sm" />
                    <Text fontSize="sm">Loading remote sinks...</Text>
                  </Flex>
                )}
              </>
            ) : (
              <Select
                data-tutorial-id="route-sink-select"
                value={sink}
                onChange={(e) => handleSinkChange(e.target.value)}
                bg={inputBg}
                placeholder="Select a sink"
              >
                {sinks.map(s => (
                  <option key={s.name} value={s.name}>
                    {s.name}
                  </option>
                ))}
              </Select>
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

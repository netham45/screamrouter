import React, { useState, useEffect, useMemo, useCallback } from 'react';
import {
  Box,
  Heading,
  Text,
  SimpleGrid,
  Card,
  CardHeader,
  CardBody,
  VStack,
  HStack,
  Spinner,
  Alert,
  AlertIcon,
  Table,
  Thead,
  Tbody,
  Tr,
  Th,
  Td,
  TableContainer,
  Button,
  useToast,
  FormControl,
  FormLabel,
  NumberInput,
  NumberInputField,
  NumberInputStepper,
  NumberIncrementStepper,
  NumberDecrementStepper,
  Collapse,
  useDisclosure,
  Switch,
  Tooltip,
} from '@chakra-ui/react';
import ApiService, { AudioEngineStats, AudioEngineSettings, BufferMetrics, Source, Sink, SourceStats, SinkStats } from '../../api/api';

const StatsPage: React.FC = () => {
  const [stats, setStats] = useState<AudioEngineStats | null>(null);
  const [settings, setSettings] = useState<AudioEngineSettings | null>(null);
  const [initialSettings, setInitialSettings] = useState<AudioEngineSettings | null>(null);
  const [sourceCatalog, setSourceCatalog] = useState<Record<string, Source>>({});
  const [sinkCatalog, setSinkCatalog] = useState<Record<string, Sink>>({});
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const toast = useToast();
  const { isOpen, onToggle } = useDisclosure();
  const formatRate = (val?: number) => (Number.isFinite(val) ? (val as number).toFixed(2) : 'N/A');
  const formatBuffer = (buf?: BufferMetrics | null) => {
    if (!buf) return 'N/A';
    const depth = Number.isFinite(buf.depth_ms) ? buf.depth_ms : 0;
    const fill = Number.isFinite(buf.fill_percent) ? buf.fill_percent : 0;
    return `${buf.size ?? 0} / ${depth.toFixed(1)} ms (${fill.toFixed(1)}%)`;
  };

  const fetchAllData = async () => {
    try {
      const [statsResponse, settingsResponse, sourcesResponse, sinksResponse] = await Promise.all([
        ApiService.getStats(),
        ApiService.getSettings(),
        ApiService.getSources({ includeTemporary: true }),
        ApiService.getSinks({ includeTemporary: true }),
      ]);
      setStats(statsResponse.data);
      setSettings(settingsResponse.data);
      setInitialSettings(JSON.parse(JSON.stringify(settingsResponse.data))); // Deep copy
      setSourceCatalog(sourcesResponse.data ?? {});
      setSinkCatalog(sinksResponse.data ?? {});
    } catch (err) {
      setError('Failed to fetch data. Please try again later.');
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchAllData();
    const interval = setInterval(() => {
      ApiService.getStats().then(response => setStats(response.data)).catch(err => console.error("Failed to fetch stats", err));
    }, 2000); // Refresh stats every 2 seconds

    return () => clearInterval(interval);
  }, []);

  const handleSettingsChange = (category: keyof AudioEngineSettings, field: string, value: string | number | boolean) => {
    if (settings) {
      setSettings(prevSettings => {
        if (!prevSettings) return null;

        const newSettings = { ...prevSettings };
        // @ts-expect-error it throws an error and i want it quiet
        newSettings[category][field] = value;

        return newSettings;
      });
    }
  };

  const handleSaveSettings = async () => {
    if (settings) {
      try {
        await ApiService.updateSettings(settings);
        toast({
          title: 'Settings saved.',
          description: 'Your audio engine settings have been updated.',
          status: 'success',
          duration: 5000,
          isClosable: true,
        });
        setInitialSettings(JSON.parse(JSON.stringify(settings))); // Update initial state
      } catch (err) {
        toast({
          title: 'Error saving settings.',
          description: 'There was an issue saving your settings.',
          status: 'error',
          duration: 5000,
          isClosable: true,
        });
        console.error(err);
      }
    }
  };

  const handleResetSettings = () => {
    if (initialSettings) {
      setSettings(JSON.parse(JSON.stringify(initialSettings))); // Deep copy
    }
  };

  const sourceLookup = useMemo(() => {
    const map = new Map<string, Source>();
    Object.values(sourceCatalog || {}).forEach((source) => {
      const keys = [
        source.name,
        source.name?.toLowerCase(),
        source.tag,
        source.tag?.toLowerCase(),
        source.ip,
        source.ip?.toLowerCase(),
      ].filter(Boolean) as string[];
      keys.forEach((key) => {
        if (!map.has(key)) {
          map.set(key, source);
        }
      });
    });
    return map;
  }, [sourceCatalog]);

  const sinkLookup = useMemo(() => {
    const map = new Map<string, Sink>();
    Object.values(sinkCatalog || {}).forEach((sinkEntry) => {
      const keys = [
        sinkEntry.config_id,
        sinkEntry.config_id?.toLowerCase(),
        sinkEntry.name,
        sinkEntry.name?.toLowerCase(),
        sinkEntry.ip,
        sinkEntry.ip?.toLowerCase(),
      ].filter(Boolean) as string[];
      keys.forEach((key) => {
        if (!map.has(key)) {
          map.set(key, sinkEntry);
        }
      });
    });
    return map;
  }, [sinkCatalog]);

  const findSourceDetails = useCallback((sourceStatId: string) => {
    if (!sourceStatId) return undefined;
    return sourceLookup.get(sourceStatId) || sourceLookup.get(sourceStatId.toLowerCase());
  }, [sourceLookup]);

  const findSourceMetadata = useCallback((sourceStat: SourceStats) => {
    return (
      findSourceDetails(sourceStat.source_tag) ||
      findSourceDetails(sourceStat.instance_id)
    );
  }, [findSourceDetails]);

  const findSinkMetadata = useCallback((sinkId: string) => {
    if (!sinkId) return undefined;
    return sinkLookup.get(sinkId) || sinkLookup.get(sinkId.toLowerCase());
  }, [sinkLookup]);

  const formatBoolean = (value?: boolean | null) => {
    if (value === undefined || value === null) {
      return 'N/A';
    }
    return value ? 'Yes' : 'No';
  };

  const formatDestination = (sinkMeta?: Sink) => {
    if (!sinkMeta || !sinkMeta.ip) {
      return 'N/A';
    }
    return sinkMeta.port ? `${sinkMeta.ip}:${sinkMeta.port}` : sinkMeta.ip;
  };

  const renderDetailLine = (label: string, value: React.ReactNode) => (
    <Text key={label} fontSize="sm">
      <Text as="span" fontWeight="bold">{label}: </Text>
      {value !== undefined && value !== null && value !== '' ? value : 'N/A'}
    </Text>
  );

  const renderSourceTooltipContent = (source: SourceStats) => {
    const metadata = findSourceMetadata(source);
    return (
      <Box maxW="360px">
        <Heading size="xs" mb={1}>Source Properties</Heading>
        <VStack align="start" spacing={0.5}>
          {renderDetailLine('Instance ID', source.instance_id)}
          {renderDetailLine('Source Tag', source.source_tag)}
          {renderDetailLine('Name', metadata?.name ?? source.source_tag)}
          {renderDetailLine('Type', metadata ? (metadata.is_group ? 'Group' : metadata.is_process ? 'Process' : 'Standard') : 'N/A')}
          {renderDetailLine('Channels', metadata?.channels)}
          {renderDetailLine('Sample Rate', metadata?.sample_rate ? `${metadata.sample_rate.toLocaleString()} Hz` : 'N/A')}
          {renderDetailLine('Bit Depth', metadata?.bit_depth)}
          {renderDetailLine('Destination / IP', metadata?.ip ?? 'N/A')}
          {renderDetailLine('Delay', metadata?.delay !== undefined ? `${metadata.delay} ms` : 'N/A')}
          {renderDetailLine('Timeshift', metadata?.timeshift !== undefined ? `${metadata.timeshift} ms` : 'N/A')}
          {renderDetailLine('Volume', metadata?.volume !== undefined ? `${metadata.volume}%` : 'N/A')}
          {renderDetailLine('Favorite', formatBoolean(metadata?.favorite))}
        </VStack>
      </Box>
    );
  };

  const renderSinkTooltipContent = (sink: SinkStats) => {
    const metadata = findSinkMetadata(sink.sink_id);
    return (
      <Box maxW="360px">
        <Heading size="xs" mb={1}>Sink Properties</Heading>
        <VStack align="start" spacing={0.5}>
          {renderDetailLine('Sink ID', sink.sink_id)}
          {renderDetailLine('Name', metadata?.name ?? sink.sink_id)}
          {renderDetailLine('Type', metadata?.protocol ?? 'N/A')}
          {renderDetailLine('Destination', formatDestination(metadata))}
          {renderDetailLine('Target Server UUID', metadata?.sap_target_host)}
          {renderDetailLine('Target Sink UUID', metadata?.sap_target_sink)}
          {renderDetailLine('Channels', metadata?.channels)}
          {renderDetailLine('Sample Rate', metadata?.sample_rate ? `${metadata.sample_rate.toLocaleString()} Hz` : 'N/A')}
          {renderDetailLine('Bit Depth', metadata?.bit_depth)}
          {renderDetailLine('Channel Layout', metadata?.channel_layout ?? 'N/A')}
          {renderDetailLine('Delay', metadata?.delay !== undefined ? `${metadata.delay} ms` : 'N/A')}
          {renderDetailLine('Timeshift', metadata?.timeshift !== undefined ? `${metadata.timeshift} ms` : 'N/A')}
          {renderDetailLine('Volume', metadata?.volume !== undefined ? `${metadata.volume}%` : 'N/A')}
          {renderDetailLine('Multi-Device', formatBoolean(metadata?.multi_device_mode))}
          {renderDetailLine('System Audio Sync', formatBoolean(metadata?.time_sync))}
        </VStack>
      </Box>
    );
  };

  if (loading) {
    return (
      <Box textAlign="center" mt="20">
        <Spinner size="xl" />
        <Text mt={4}>Loading Stats...</Text>
      </Box>
    );
  }

  if (error) {
    return (
      <Alert status="error" mt="10">
        <AlertIcon />
        {error}
      </Alert>
    );
  }

  if (!stats) {
    return <Text>No stats available.</Text>;
  }

  const renderTuningControl = (category: keyof AudioEngineSettings, field: string, label: string, step = 1, isSwitch = false) => {
    if (!settings) return null;
    const value = settings[category][field as keyof typeof settings[typeof category]];
    const numericValue = typeof value === 'number' && !Number.isNaN(value) ? value : 0;

    return (
      <FormControl>
        <FormLabel>{label}</FormLabel>
        {isSwitch ? (
          <Switch isChecked={Boolean(value)} onChange={(e) => handleSettingsChange(category, field, e.target.checked)} />
        ) : (
          <NumberInput
            value={numericValue}
            onChange={(valueString) => {
              const nextValue = valueString === '' ? 0 : parseFloat(valueString);
              handleSettingsChange(category, field, Number.isNaN(nextValue) ? numericValue : nextValue);
            }}
            step={step}
          >
            <NumberInputField />
            <NumberInputStepper>
              <NumberIncrementStepper />
              <NumberDecrementStepper />
            </NumberInputStepper>
          </NumberInput>
        )}
      </FormControl>
    );
  };

  return (
    <Box p={5}>
      <Heading as="h1" mb={6} textAlign="center">
        Audio Engine Statistics
      </Heading>

      <Card mb={6}>
        <CardHeader>
          <Heading size="md">Global Stats</Heading>
        </CardHeader>
        <CardBody>
          <SimpleGrid columns={{ base: 1, md: 2 }} spacing={5}>
            <VStack align="start">
              <Text fontWeight="bold">Timeshift Buffer Total Size:</Text>
              <Text>{stats.global_stats.timeshift_buffer_total_size?.toLocaleString() ?? 'N/A'}</Text>
            </VStack>
            <VStack align="start">
              <Text fontWeight="bold">Packets Added to Timeshift/sec:</Text>
              <Text>{stats.global_stats.packets_added_to_timeshift_per_second?.toFixed(2) ?? 'N/A'}</Text>
            </VStack>
            <VStack align="start">
              <Text fontWeight="bold">Inbound Queue:</Text>
              <Text>
                {formatBuffer(stats.global_stats.timeshift_inbound_buffer)} | push {formatRate(stats.global_stats.timeshift_inbound_buffer?.push_rate_per_second)} pps | pop {formatRate(stats.global_stats.timeshift_inbound_buffer?.pop_rate_per_second)} pps
              </Text>
            </VStack>
          </SimpleGrid>
        </CardBody>
      </Card>

      <Card mb={6}>
        <CardHeader>
          <Heading size="md">Source Input Stream Stats</Heading>
        </CardHeader>
        <CardBody>
          <TableContainer>
            <Table variant="simple">
              <Thead>
                <Tr>
                  <Th>Stream Tag</Th>
                  <Th isNumeric>Jitter (ms)</Th>
                  <Th isNumeric>Packets/sec</Th>
                  <Th isNumeric>Late Packets</Th>
                  <Th isNumeric>Lagging Events</Th>
                  <Th isNumeric>TM Underruns</Th>
                  <Th isNumeric>TM Discards</Th>
                  <Th isNumeric>Arrival Error (ms)</Th>
                  <Th isNumeric>Anchor Drift (ms)</Th>
                  <Th isNumeric>Target Buffer (ms)</Th>
                  <Th isNumeric>Available Chunks</Th>
                  <Th isNumeric>Buffer Fill %</Th>
                  <Th isNumeric>Playback Rate</Th>
                  <Th isNumeric>TS Depth (ms)</Th>
                  <Th isNumeric>System Jitter (ms)</Th>
                  <Th isNumeric>System Delay (ms)</Th>
                  <Th isNumeric>Total Packets</Th>
                </Tr>
              </Thead>
              <Tbody>
                {Object.entries(stats.stream_stats).map(([tag, stream]) => (
                  <Tr key={tag}>
                    <Td>{tag}</Td>
                    <Td isNumeric>{stream.jitter_estimate_ms?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.packets_per_second?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.timeshift_buffer_late_packets?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.timeshift_buffer_lagging_events?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.tm_buffer_underruns?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.tm_packets_discarded?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.last_arrival_time_error_ms?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.total_anchor_adjustment_ms?.toFixed(4) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.target_buffer_level_ms?.toFixed(0) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.timeshift_buffer_size?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.buffer_target_fill_percentage?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.playback_rate?.toFixed(4) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.timeshift_buffer?.depth_ms ? stream.timeshift_buffer.depth_ms.toFixed(1) : 'N/A'}</Td>
                    <Td isNumeric>{stream.system_jitter_ms?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.last_system_delay_ms?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.total_packets_in_stream?.toLocaleString() ?? 'N/A'}</Td>
                  </Tr>
                ))}
              </Tbody>
            </Table>
          </TableContainer>
        </CardBody>
      </Card>

      <Card mb={6}>
        <CardHeader>
          <Heading size="md">Source Input Stream Processor Stats</Heading>
        </CardHeader>
        <CardBody>
          <TableContainer>
            <Table variant="simple">
              <Thead>
                <Tr>
                  <Th>Instance ID</Th>
                  <Th>Source Tag</Th>
                  <Th isNumeric>Input Queue</Th>
                  <Th isNumeric>Input Depth (ms)</Th>
                  <Th isNumeric>Input Push/sec</Th>
                  <Th isNumeric>Input Pop/sec</Th>
                  <Th isNumeric>Output Queue</Th>
                  <Th isNumeric>Output Depth (ms)</Th>
                  <Th isNumeric>Output Push/sec</Th>
                  <Th isNumeric>Process Buffer (ms)</Th>
                  <Th isNumeric>Processed/sec</Th>
                  <Th isNumeric>Playback Rate</Th>
                  <Th isNumeric>Resample Ratio</Th>
                  <Th isNumeric>Chunks Pushed</Th>
                  <Th isNumeric>Discarded</Th>
                  <Th isNumeric>Reconfigurations</Th>
                </Tr>
              </Thead>
              <Tbody>
                {stats.source_stats.map((source) => (
                  <Tooltip key={source.instance_id} label={renderSourceTooltipContent(source)} hasArrow openDelay={300} placement="auto">
                    <Tr>
                      <Td>{source.instance_id}</Td>
                      <Td>{source.source_tag}</Td>
                      <Td isNumeric>{source.input_queue_size?.toLocaleString() ?? 'N/A'}</Td>
                      <Td isNumeric>{source.input_buffer?.depth_ms !== undefined ? source.input_buffer.depth_ms.toFixed(2) : 'N/A'}</Td>
                      <Td isNumeric>{formatRate(source.input_buffer?.push_rate_per_second)}</Td>
                      <Td isNumeric>{formatRate(source.input_buffer?.pop_rate_per_second)}</Td>
                      <Td isNumeric>{source.output_queue_size?.toLocaleString() ?? 'N/A'}</Td>
                      <Td isNumeric>{source.output_buffer?.depth_ms !== undefined ? source.output_buffer.depth_ms.toFixed(2) : 'N/A'}</Td>
                      <Td isNumeric>{formatRate(source.output_buffer?.push_rate_per_second)}</Td>
                      <Td isNumeric>{source.process_buffer?.depth_ms !== undefined ? source.process_buffer.depth_ms.toFixed(2) : 'N/A'}</Td>
                      <Td isNumeric>{source.packets_processed_per_second?.toFixed(2) ?? 'N/A'}</Td>
                      <Td isNumeric>{source.playback_rate?.toFixed(4) ?? 'N/A'}</Td>
                      <Td isNumeric>{source.resample_ratio?.toFixed(4) ?? 'N/A'}</Td>
                      <Td isNumeric>{source.chunks_pushed?.toLocaleString() ?? 'N/A'}</Td>
                      <Td isNumeric>{source.discarded_packets?.toLocaleString() ?? 'N/A'}</Td>
                      <Td isNumeric>{source.reconfigurations?.toLocaleString() ?? 'N/A'}</Td>
                    </Tr>
                  </Tooltip>
                ))}
              </Tbody>
            </Table>
          </TableContainer>
        </CardBody>
      </Card>

      <Card mb={6}>
        <CardHeader>
          <Heading size="md">Sink Stats</Heading>
        </CardHeader>
        <CardBody>
          {stats.sink_stats.map((sink) => (
            <Tooltip key={sink.sink_id} label={renderSinkTooltipContent(sink)} hasArrow openDelay={300} placement="auto">
              <Box mb={4} p={4} borderWidth="1px" borderRadius="md">
                <Heading size="sm" mb={2}>{sink.sink_id}</Heading>
                <HStack spacing={10} mb={4}>
                <VStack align="start">
                  <Text fontWeight="bold">Active Streams:</Text>
                  <Text>{sink.active_input_streams?.toLocaleString() ?? 'N/A'}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">Total Streams:</Text>
                  <Text>{sink.total_input_streams?.toLocaleString() ?? 'N/A'}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">Mixed/sec:</Text>
                  <Text>{sink.packets_mixed_per_second?.toFixed(2) ?? 'N/A'}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">Buffer Underruns:</Text>
                  <Text>{sink.sink_buffer_underruns?.toLocaleString() ?? 'N/A'}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">Buffer Overflows:</Text>
                  <Text>{sink.sink_buffer_overflows?.toLocaleString() ?? 'N/A'}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">MP3 Overflows:</Text>
                  <Text>{sink.mp3_buffer_overflows?.toLocaleString() ?? 'N/A'}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">Payload Buffer:</Text>
                  <Text>{formatBuffer(sink.payload_buffer)}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">MP3 Out Buffer:</Text>
                  <Text>{formatBuffer(sink.mp3_output_buffer)}</Text>
                </VStack>
                <VStack align="start">
                  <Text fontWeight="bold">MP3 PCM Buffer:</Text>
                  <Text>{formatBuffer(sink.mp3_pcm_buffer)}</Text>
                </VStack>
              </HStack>
              <HStack spacing={6} mb={4}>
                <Text>Last mix dwell: {sink.last_chunk_dwell_ms !== undefined ? sink.last_chunk_dwell_ms.toFixed(2) : 'N/A'} ms</Text>
                <Text>Avg mix dwell: {sink.avg_chunk_dwell_ms !== undefined ? sink.avg_chunk_dwell_ms.toFixed(2) : 'N/A'} ms</Text>
                <Text>Send gap (avg/last): {sink.avg_send_gap_ms !== undefined ? sink.avg_send_gap_ms.toFixed(2) : 'N/A'} / {sink.last_send_gap_ms !== undefined ? sink.last_send_gap_ms.toFixed(2) : 'N/A'} ms</Text>
              </HStack>
              {sink.webrtc_listeners && sink.webrtc_listeners.length > 0 && (
                <>
                  <Heading size="xs" mb={2}>WebRTC Listeners</Heading>
                  <TableContainer>
                    <Table variant="simple" size="sm">
                      <Thead>
                        <Tr>
                          <Th>Listener ID</Th>
                          <Th>State</Th>
                          <Th isNumeric>PCM Buffer</Th>
                          <Th isNumeric>Sent/sec</Th>
                        </Tr>
                      </Thead>
                      <Tbody>
                        {sink.webrtc_listeners.map((listener) => (
                          <Tr key={listener.listener_id}>
                            <Td>{listener.listener_id}</Td>
                            <Td>{listener.connection_state}</Td>
                            <Td isNumeric>{listener.pcm_buffer_size?.toLocaleString() ?? 'N/A'}</Td>
                            <Td isNumeric>{listener.packets_sent_per_second?.toFixed(2) ?? 'N/A'}</Td>
                          </Tr>
                        ))}
                      </Tbody>
                    </Table>
                  </TableContainer>
                </>
              )}
              {sink.inputs && sink.inputs.length > 0 && (
                <>
                  <Heading size="xs" mt={4} mb={2}>Input Lanes</Heading>
                  <TableContainer>
                    <Table variant="simple" size="sm">
                      <Thead>
                        <Tr>
                          <Th>Instance</Th>
                          <Th>Source Queue</Th>
                          <Th>Ready Queue</Th>
                          <Th isNumeric>Ready Push/sec</Th>
                          <Th isNumeric>Ready Pop/sec</Th>
                          <Th isNumeric>Dwell (ms)</Th>
                          <Th isNumeric>Underruns</Th>
                        </Tr>
                      </Thead>
                      <Tbody>
                        {sink.inputs.map((lane) => (
                          <Tr key={`${sink.sink_id}-${lane.instance_id}`}>
                            <Td>{lane.instance_id}</Td>
                            <Td>{formatBuffer(lane.source_output_queue)}</Td>
                            <Td>{formatBuffer(lane.ready_queue)}</Td>
                            <Td isNumeric>{formatRate(lane.ready_queue?.push_rate_per_second)}</Td>
                            <Td isNumeric>{formatRate(lane.ready_queue?.pop_rate_per_second)}</Td>
                            <Td isNumeric>{lane.last_chunk_dwell_ms !== undefined ? lane.last_chunk_dwell_ms.toFixed(2) : 'N/A'}</Td>
                            <Td isNumeric>{lane.underrun_events?.toLocaleString() ?? 'N/A'}</Td>
                          </Tr>
                        ))}
                      </Tbody>
                    </Table>
                  </TableContainer>
                </>
              )}
              </Box>
            </Tooltip>
          ))}
        </CardBody>
      </Card>

      <Card>
        <CardHeader>
          <HStack justify="space-between">
            <Heading size="md">Audio Engine Tuning</Heading>
            <Button onClick={onToggle}>{isOpen ? 'Hide' : 'Show'}</Button>
          </HStack>
        </CardHeader>
        <Collapse in={isOpen} animateOpacity>
          <CardBody>
            {settings && (
              <VStack spacing={6} align="stretch">
                <Box>
                  <Heading size="sm" mb={4}>Timeshift Tuning</Heading>
                  <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4}>
                    {renderTuningControl('timeshift_tuning', 'cleanup_interval_ms', 'Cleanup Interval (ms)')}
                    {renderTuningControl('timeshift_tuning', 'late_packet_threshold_ms', 'Late Packet Threshold (ms)', 1)}
                    {renderTuningControl('timeshift_tuning', 'target_buffer_level_ms', 'Target Buffer Level (ms)')}
                    {renderTuningControl('timeshift_tuning', 'loop_max_sleep_ms', 'Loop Max Sleep (ms)')}
                    {renderTuningControl('timeshift_tuning', 'max_catchup_lag_ms', 'Max Catch-up Lag (ms)')}
                    {renderTuningControl('timeshift_tuning', 'max_clock_pending_packets', 'Max Clock Pending Packets')}
                    {renderTuningControl('timeshift_tuning', 'rtp_continuity_slack_seconds', 'RTP Continuity Slack (s)', 0.01)}
                    {renderTuningControl('timeshift_tuning', 'rtp_session_reset_threshold_seconds', 'RTP Session Reset Threshold (s)', 0.01)}
                    {renderTuningControl('timeshift_tuning', 'playback_rate_adjustment_enabled', 'Playback Rate Adjustment', 1, true)}
                  </SimpleGrid>
                </Box>

                <Box>
                  <Heading size="sm" mb={4}>Mixer Tuning</Heading>
                  <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4}>
                    {renderTuningControl('mixer_tuning', 'mp3_bitrate_kbps', 'MP3 Bitrate (kbps)')}
                    {renderTuningControl('mixer_tuning', 'mp3_vbr_enabled', 'MP3 VBR Enabled', 1, true)}
                    {renderTuningControl('mixer_tuning', 'mp3_output_queue_max_size', 'MP3 Output Queue Max Size')}
                    {renderTuningControl('mixer_tuning', 'underrun_hold_timeout_ms', 'Underrun Hold Timeout (ms)')}
                    {renderTuningControl('mixer_tuning', 'max_input_queue_chunks', 'Max Source Output Chunks')}
                    {renderTuningControl('mixer_tuning', 'min_input_queue_chunks', 'Min Source Output Chunks')}
                    {renderTuningControl('mixer_tuning', 'max_ready_chunks_per_source', 'Max Mix Ready Chunks per Source')}
                    {renderTuningControl('mixer_tuning', 'max_queued_chunks', 'Max Queued Chunks')}
                  </SimpleGrid>
                </Box>

                <Box>
                  <Heading size="sm" mb={4}>Source Processor Tuning</Heading>
                  <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4}>
                    {renderTuningControl('source_processor_tuning', 'command_loop_sleep_ms', 'Command Loop Sleep (ms)')}
                    {renderTuningControl('source_processor_tuning', 'discontinuity_threshold_ms', 'Discontinuity Threshold (ms)')}
                  </SimpleGrid>
                </Box>

                <Box>
                  <Heading size="sm" mb={4}>Processor Tuning</Heading>
                  <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4}>
                    {renderTuningControl('processor_tuning', 'oversampling_factor', 'Oversampling Factor')}
                    {renderTuningControl('processor_tuning', 'volume_smoothing_factor', 'Volume Smoothing Factor', 0.001)}
                    {renderTuningControl('processor_tuning', 'dc_filter_cutoff_hz', 'DC Filter Cutoff (Hz)', 1)}
                    {renderTuningControl('processor_tuning', 'normalization_target_rms', 'Normalization Target RMS', 0.1)}
                    {renderTuningControl('processor_tuning', 'normalization_attack_smoothing', 'Normalization Attack Smoothing', 0.01)}
                    {renderTuningControl('processor_tuning', 'normalization_decay_smoothing', 'Normalization Decay Smoothing', 0.01)}
                    {renderTuningControl('processor_tuning', 'dither_noise_shaping_factor', 'Dither Noise Shaping Factor', 0.01)}
                  </SimpleGrid>
                </Box>

                <Box>
                  <Heading size="sm" mb={4}>System Audio Tuning</Heading>
                  <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4}>
                    {renderTuningControl('system_audio_tuning', 'alsa_target_latency_ms', 'ALSA Target Latency (ms)')}
                    {renderTuningControl('system_audio_tuning', 'alsa_periods_per_buffer', 'ALSA Periods per Buffer')}
                    {renderTuningControl('system_audio_tuning', 'alsa_dynamic_latency_enabled', 'Enable Dynamic Latency', 1, true)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_min_ms', 'Latency Minimum (ms)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_max_ms', 'Latency Maximum (ms)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_low_water_ms', 'Low Buffer Threshold (ms)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_high_water_ms', 'High Buffer Threshold (ms)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_integral_gain', 'Latency Integral Gain', 0.01)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_rate_limit_ms_per_sec', 'Latency Rate Limit (ms/sec)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_idle_decay_ms_per_sec', 'Idle Decay (ms/sec)', 0.1)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_apply_hysteresis_ms', 'Latency Hysteresis (ms)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_reconfig_cooldown_ms', 'Reconfigure Cooldown (ms)', 10)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_xrun_boost_ms', 'X-run Boost (ms)', 0.5)}
                    {renderTuningControl('system_audio_tuning', 'alsa_latency_low_step_ms', 'Low Buffer Step (ms)', 0.5)}
                  </SimpleGrid>
                </Box>

                <Box>
                  <Heading size="sm" mb={4}>RTP Receiver Tuning</Heading>
                  <SimpleGrid columns={{ base: 1, md: 3 }} spacing={4}>
                    {renderTuningControl('rtp_receiver_tuning', 'format_probe_duration_ms', 'Format Probe Duration (ms)', 50)}
                    {renderTuningControl('rtp_receiver_tuning', 'format_probe_min_bytes', 'Format Probe Min Bytes', 100)}
                  </SimpleGrid>
                </Box>

                <HStack justify="flex-end" spacing={4}>
                  <Button onClick={handleResetSettings}>Reset</Button>
                  <Button colorScheme="blue" onClick={handleSaveSettings}>Save</Button>
                </HStack>
              </VStack>
            )}
          </CardBody>
        </Collapse>
      </Card>
    </Box>
  );
};

export default StatsPage;

import React, { useState, useEffect } from 'react';
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
} from '@chakra-ui/react';
import ApiService, { AudioEngineStats, AudioEngineSettings, BufferMetrics } from '../../api/api';

const StatsPage: React.FC = () => {
  const [stats, setStats] = useState<AudioEngineStats | null>(null);
  const [settings, setSettings] = useState<AudioEngineSettings | null>(null);
  const [initialSettings, setInitialSettings] = useState<AudioEngineSettings | null>(null);
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
      const [statsResponse, settingsResponse] = await Promise.all([
        ApiService.getStats(),
        ApiService.getSettings(),
      ]);
      setStats(statsResponse.data);
      setSettings(settingsResponse.data);
      setInitialSettings(JSON.parse(JSON.stringify(settingsResponse.data))); // Deep copy
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
                  <Tr key={source.instance_id}>
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
            <Box key={sink.sink_id} mb={4} p={4} borderWidth="1px" borderRadius="md">
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

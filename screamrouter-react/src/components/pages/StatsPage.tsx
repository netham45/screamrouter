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
} from '@chakra-ui/react';
import ApiService, { AudioEngineStats } from '../../api/api';

const StatsPage: React.FC = () => {
  const [stats, setStats] = useState<AudioEngineStats | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchStats = async () => {
      try {
        const response = await ApiService.getStats();
        setStats(response.data);
      } catch (err) {
        setError('Failed to fetch stats. Please try again later.');
        console.error(err);
      } finally {
        setLoading(false);
      }
    };

    fetchStats();
    const interval = setInterval(fetchStats, 2000); // Refresh every 2 seconds

    return () => clearInterval(interval);
  }, []);

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
          </SimpleGrid>
        </CardBody>
      </Card>

      <Card mb={6}>
        <CardHeader>
          <Heading size="md">Stream Stats</Heading>
        </CardHeader>
        <CardBody>
          <TableContainer>
            <Table variant="simple">
              <Thead>
                <Tr>
                  <Th>Stream Tag</Th>
                  <Th isNumeric>Jitter (ms)</Th>
                  <Th isNumeric>Packets/sec</Th>
                  <Th isNumeric>Timeshift Buffer Size</Th>
                </Tr>
              </Thead>
              <Tbody>
                {Object.entries(stats.stream_stats).map(([tag, stream]) => (
                  <Tr key={tag}>
                    <Td>{tag}</Td>
                    <Td isNumeric>{stream.jitter_estimate_ms?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.packets_per_second?.toFixed(2) ?? 'N/A'}</Td>
                    <Td isNumeric>{stream.timeshift_buffer_size?.toLocaleString() ?? 'N/A'}</Td>
                  </Tr>
                ))}
              </Tbody>
            </Table>
          </TableContainer>
        </CardBody>
      </Card>

      <Card mb={6}>
        <CardHeader>
          <Heading size="md">Source Stats</Heading>
        </CardHeader>
        <CardBody>
          <TableContainer>
            <Table variant="simple">
              <Thead>
                <Tr>
                  <Th>Instance ID</Th>
                  <Th>Source Tag</Th>
                  <Th isNumeric>Input Queue</Th>
                  <Th isNumeric>Output Queue</Th>
                  <Th isNumeric>Processed/sec</Th>
                </Tr>
              </Thead>
              <Tbody>
                {stats.source_stats.map((source) => (
                  <Tr key={source.instance_id}>
                    <Td>{source.instance_id}</Td>
                    <Td>{source.source_tag}</Td>
                    <Td isNumeric>{source.input_queue_size?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{source.output_queue_size?.toLocaleString() ?? 'N/A'}</Td>
                    <Td isNumeric>{source.packets_processed_per_second?.toFixed(2) ?? 'N/A'}</Td>
                  </Tr>
                ))}
              </Tbody>
            </Table>
          </TableContainer>
        </CardBody>
      </Card>

      <Card>
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
            </Box>
          ))}
        </CardBody>
      </Card>
    </Box>
  );
};

export default StatsPage;
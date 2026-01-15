import React, { useState, useEffect, useRef, useCallback } from 'react';
import {
  Box,
  VStack,
  HStack,
  Text,
  Select,
  Button,
  Checkbox,
  IconButton,
  Spinner,
  Alert,
  AlertIcon,
  Badge,
  useColorModeValue,
  Tooltip,
  Flex,
  Spacer,
  Input,
} from '@chakra-ui/react';
import {
  FaPlay,
  FaPause,
  FaStop,
  FaTrash,
  FaSync,
  FaDownload,
} from 'react-icons/fa';

interface LogFile {
  filename: string;
  size: number;
  modified: string;
  lines: number;
}

interface LogMessage {
  type: 'initial' | 'append' | 'error';
  lines?: string[];
  line?: string;
  message?: string;
}

const LogViewerContent: React.FC = () => {
  const [logFiles, setLogFiles] = useState<LogFile[]>([]);
  const [selectedFile, setSelectedFile] = useState<string>('');
  const [initialLines, setInitialLines] = useState<number>(100);
  const [logContent, setLogContent] = useState<string[]>([]);
  const [isStreaming, setIsStreaming] = useState<boolean>(false);
  const [isPaused, setIsPaused] = useState<boolean>(false);
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [error, setError] = useState<string | null>(null);
  const [autoScroll, setAutoScroll] = useState<boolean>(true);
  const [connectionStatus, setConnectionStatus] = useState<'disconnected' | 'connecting' | 'connected' | 'error'>('disconnected');
  const [searchFilter, setSearchFilter] = useState<string>('');

  const wsRef = useRef<WebSocket | null>(null);
  const terminalRef = useRef<HTMLDivElement>(null);
  const reconnectTimeoutRef = useRef<NodeJS.Timeout | null>(null);

  // Color scheme
  const bgColor = useColorModeValue('gray.50', 'gray.900');
  const terminalBg = useColorModeValue('black', '#1a1a1a');
  const terminalText = useColorModeValue('#00ff00', '#00ff00');
  const borderColor = useColorModeValue('gray.200', 'gray.700');
  const cardBg = useColorModeValue('white', 'gray.800');

  // Fetch available log files
  const fetchLogFiles = useCallback(async () => {
    setIsLoading(true);
    setError(null);
    try {
      const response = await fetch('/api/logs');
      if (!response.ok) {
        throw new Error(`Failed to fetch logs: ${response.statusText}`);
      }
      const data = await response.json();
      setLogFiles(data.logs || []);

      // Auto-select first log if none selected
      if (!selectedFile && data.logs && data.logs.length > 0) {
        setSelectedFile(data.logs[0].filename);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch log files');
      console.error('Error fetching log files:', err);
    } finally {
      setIsLoading(false);
    }
  }, [selectedFile]);

  // Connect to WebSocket for log streaming
  const connectWebSocket = useCallback(() => {
    if (!selectedFile) {
      return;
    }

    // Clean up existing connection if any
    disconnectWebSocket();

    setConnectionStatus('connecting');
    setError(null);

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/logs/${encodeURIComponent(selectedFile)}/${initialLines}`;

    console.log('Connecting to WebSocket:', wsUrl);

    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

    ws.onopen = () => {
      console.log('WebSocket connected');
      setConnectionStatus('connected');
      setIsStreaming(true);
      setIsPaused(false);
    };

    ws.onmessage = (event) => {
      try {
        const message: LogMessage = JSON.parse(event.data);

        if (message.type === 'initial' && message.lines) {
          setLogContent(message.lines);
        } else if (message.type === 'append' && message.line) {
          setLogContent(prev => [...prev, message.line!]);
        } else if (message.type === 'error' && message.message) {
          setError(message.message);
        }
      } catch (err) {
        console.error('Error parsing WebSocket message:', err);
      }
    };

    ws.onerror = (event) => {
      console.error('WebSocket error:', event);
      setConnectionStatus('error');
      setError('WebSocket connection error');
    };

    ws.onclose = (event) => {
      console.log('WebSocket closed:', event.code, event.reason);
      setConnectionStatus('disconnected');
      setIsStreaming(false);
      wsRef.current = null;

      // Auto-reconnect if not manually closed
      if (event.code !== 1000 && selectedFile) {
        reconnectTimeoutRef.current = setTimeout(() => {
          console.log('Attempting to reconnect...');
          connectWebSocket();
        }, 3000);
      }
    };
  }, [selectedFile, initialLines]);

  // Disconnect WebSocket
  const disconnectWebSocket = useCallback(() => {
    if (reconnectTimeoutRef.current) {
      clearTimeout(reconnectTimeoutRef.current);
      reconnectTimeoutRef.current = null;
    }

    if (wsRef.current) {
      wsRef.current.close(1000, 'User disconnected');
      wsRef.current = null;
    }

    setConnectionStatus('disconnected');
    setIsStreaming(false);
  }, []);

  // Toggle pause/resume
  const togglePause = useCallback(() => {
    if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN) {
      return;
    }

    const action = isPaused ? 'resume' : 'pause';
    wsRef.current.send(JSON.stringify({ action }));
    setIsPaused(!isPaused);
  }, [isPaused]);

  // Clear log display
  const clearDisplay = useCallback(() => {
    setLogContent([]);
  }, []);

  // Download log content
  const downloadLog = useCallback(() => {
    const content = logContent.join('\n');
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = selectedFile || 'log.txt';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }, [logContent, selectedFile]);

  // Auto-scroll to bottom when new content arrives
  useEffect(() => {
    if (autoScroll && terminalRef.current) {
      terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
    }
  }, [logContent, autoScroll]);

  // Fetch log files on mount
  useEffect(() => {
    fetchLogFiles();
  }, [fetchLogFiles]);

  // Don't auto-connect, let user control it with Start/Stop button

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close(1000, 'Component unmounting');
      }
    };
  }, []);

  // Parse log level from line content
  const getLogColor = (line: string): string => {
    if (line.includes('[ERROR]') || line.includes('ERROR:')) return '#ff4444';
    if (line.includes('[WARNING]') || line.includes('WARNING:') || line.includes('[WARN]')) return '#ffaa00';
    if (line.includes('[INFO]') || line.includes('INFO:')) return '#00aaff';
    if (line.includes('[DEBUG]') || line.includes('DEBUG:')) return '#888888';
    return terminalText;
  };

  const getStatusColor = () => {
    switch (connectionStatus) {
      case 'connected': return 'green';
      case 'connecting': return 'yellow';
      case 'error': return 'red';
      default: return 'gray';
    }
  };

  return (
    <Box h="100%" bg={bgColor} p={4}>
      <VStack spacing={4} align="stretch" h="100%">
        {/* Header */}
        <Box>
          <Text fontSize="2xl" fontWeight="bold" mb={2}>
            Log Viewer
          </Text>
          <Text fontSize="sm" color="gray.500">
            View and stream log files in real-time
          </Text>
        </Box>

        {/* Controls */}
        <Box bg={cardBg} p={4} borderRadius="md" borderWidth="1px" borderColor={borderColor}>
          <VStack spacing={3} align="stretch">
            <HStack spacing={3} flexWrap="wrap">
              {/* Search filter */}
              <Box minW="150px">
                <Text fontSize="sm" mb={1}>Search</Text>
                <Input
                  placeholder="Filter files..."
                  value={searchFilter}
                  onChange={(e) => setSearchFilter(e.target.value)}
                  size="sm"
                />
              </Box>

              {/* Log file selector */}
              <Box minW="200px">
                <Text fontSize="sm" mb={1}>Log File</Text>
                <Select
                  value={selectedFile}
                  onChange={(e) => {
                    disconnectWebSocket();
                    setLogContent([]);
                    setSelectedFile(e.target.value);
                  }}
                  size="sm"
                  isDisabled={isLoading}
                >
                  <option value="">Select a log file</option>
                  {logFiles
                    .filter((file) => file.filename.toLowerCase().includes(searchFilter.toLowerCase()))
                    .map((file) => (
                      <option key={file.filename} value={file.filename}>
                        {file.filename} ({file.lines} lines, {(file.size / 1024).toFixed(1)} KB)
                      </option>
                    ))}
                </Select>
              </Box>

              {/* Initial lines selector */}
              <Box minW="120px">
                <Text fontSize="sm" mb={1}>Initial Lines</Text>
                <Select
                  value={initialLines}
                  onChange={(e) => {
                    const value = parseInt(e.target.value);
                    setInitialLines(value);
                    // Don't reconnect automatically, user needs to click Start
                    if (isStreaming) {
                      disconnectWebSocket();
                      setLogContent([]);
                    }
                  }}
                  size="sm"
                >
                  <option value={100}>Last 100</option>
                  <option value={500}>Last 500</option>
                  <option value={2000}>Last 2000</option>
                  <option value={-1}>All</option>
                </Select>
              </Box>

              {/* Stream controls */}
              <HStack>
                <Button
                  leftIcon={isStreaming ? <FaStop /> : <FaPlay />}
                  colorScheme={isStreaming ? 'red' : 'green'}
                  size="sm"
                  onClick={() => {
                    if (isStreaming) {
                      disconnectWebSocket();
                    } else {
                      connectWebSocket();
                    }
                  }}
                  isDisabled={!selectedFile}
                >
                  {isStreaming ? 'Stop' : 'Start'}
                </Button>

                <Button
                  leftIcon={isPaused ? <FaPlay /> : <FaPause />}
                  size="sm"
                  onClick={togglePause}
                  isDisabled={!isStreaming}
                >
                  {isPaused ? 'Resume' : 'Pause'}
                </Button>
              </HStack>

              {/* Action buttons */}
              <HStack>
                <Tooltip label="Clear display">
                  <IconButton
                    aria-label="Clear display"
                    icon={<FaTrash />}
                    size="sm"
                    onClick={clearDisplay}
                    isDisabled={logContent.length === 0}
                  />
                </Tooltip>

                <Tooltip label="Refresh log files">
                  <IconButton
                    aria-label="Refresh"
                    icon={<FaSync />}
                    size="sm"
                    onClick={fetchLogFiles}
                    isDisabled={isLoading}
                  />
                </Tooltip>

                <Tooltip label="Download log">
                  <IconButton
                    aria-label="Download"
                    icon={<FaDownload />}
                    size="sm"
                    onClick={downloadLog}
                    isDisabled={logContent.length === 0}
                  />
                </Tooltip>
              </HStack>

              <Spacer />

              {/* Auto-scroll checkbox */}
              <Checkbox
                isChecked={autoScroll}
                onChange={(e) => setAutoScroll(e.target.checked)}
                size="sm"
              >
                Auto-scroll
              </Checkbox>

              {/* Connection status */}
              <Badge colorScheme={getStatusColor()} variant="outline">
                {connectionStatus}
              </Badge>
            </HStack>
          </VStack>
        </Box>

        {/* Error display */}
        {error && (
          <Alert status="error" borderRadius="md">
            <AlertIcon />
            {error}
          </Alert>
        )}

        {/* Terminal display */}
        <Box
          ref={terminalRef}
          bg={terminalBg}
          color={terminalText}
          fontFamily="'Courier New', Courier, monospace"
          fontSize="12px"
          p={4}
          borderRadius="md"
          borderWidth="1px"
          borderColor={borderColor}
          overflowY="auto"
          flex="1"
          css={{
            '&::-webkit-scrollbar': {
              width: '8px',
            },
            '&::-webkit-scrollbar-track': {
              backgroundColor: 'rgba(255, 255, 255, 0.1)',
            },
            '&::-webkit-scrollbar-thumb': {
              backgroundColor: 'rgba(255, 255, 255, 0.3)',
              borderRadius: '4px',
            },
          }}
        >
          {isLoading ? (
            <Flex justify="center" align="center" h="100%">
              <Spinner color={terminalText} />
            </Flex>
          ) : logContent.length === 0 ? (
            <Flex justify="center" align="center" h="100%">
              <Text color="gray.600">
                {selectedFile ? 'No log content to display' : 'Select a log file to view'}
              </Text>
            </Flex>
          ) : (
            logContent.map((line, index) => (
              <Box key={index} whiteSpace="pre-wrap" wordBreak="break-all" lineHeight="1.4">
                <Text as="span" color="gray.600" mr={2} userSelect="none" display="inline-block" minW="50px" textAlign="right">
                  {index + 1}
                </Text>
                <Text as="span" color={getLogColor(line)}>
                  {line}
                </Text>
              </Box>
            ))
          )}
        </Box>

        {/* Status bar */}
        <HStack fontSize="sm" color="gray.500">
          <Text>{logContent.length} lines loaded</Text>
          <Spacer />
          {selectedFile && <Text>{selectedFile}</Text>}
        </HStack>
      </VStack>
    </Box>
  );
};

export default LogViewerContent;
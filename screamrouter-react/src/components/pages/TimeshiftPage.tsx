import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  AlertIcon,
  Box,
  Button,
  Card,
  CardBody,
  CardHeader,
  Divider,
  Flex,
  FormControl,
  FormLabel,
  Grid,
  GridItem,
  Heading,
  HStack,
  Icon,
  NumberInput,
  NumberInputField,
  NumberInputStepper,
  NumberIncrementStepper,
  NumberDecrementStepper,
  Select,
  Slider,
  SliderFilledTrack,
  SliderThumb,
  SliderTrack,
  Spinner,
  Stack,
  Stat,
  StatLabel,
  StatNumber,
  Text,
  Tooltip,
  useColorModeValue,
  useToast,
  VStack,
} from '@chakra-ui/react';
import { FaClipboard, FaDownload, FaPlay, FaStop, FaSyncAlt } from 'react-icons/fa';
import { useAppContext } from '../../context/AppContext';
import TimeshiftViewer, { TimeRange } from '../timeshift/TimeshiftViewer';
import { fetchTimeshiftRecording } from '../../api/timeshift';
import {
  TimeshiftAnalysis,
  buildWavBlob,
  convertRecordingToAnalysis,
  extractPCMWindow,
} from '../../utils/timeshiftProcessing';

const sanitizeFilenameFragment = (value: string): string => value.replace(/[^A-Za-z0-9_.-]+/g, '_') || 'timeshift';

const formatSeconds = (seconds: number): string => {
  if (!Number.isFinite(seconds)) {
    return '0.00 s';
  }
  if (seconds >= 60) {
    const minutes = Math.floor(seconds / 60);
    const remainder = seconds - minutes * 60;
    return `${minutes}m ${remainder.toFixed(1)}s`;
  }
  return `${seconds.toFixed(2)} s`;
};

const TimeshiftPage: React.FC = () => {
  const { sources } = useAppContext();
  const toast = useToast();
  const initialSourceFromQuery = useMemo(() => {
    if (typeof window === 'undefined') {
      return '';
    }
    const params = new URLSearchParams(window.location.search);
    return params.get('source') ?? '';
  }, []);
  const sourceOptions = useMemo(
    () =>
      sources
        .map((source) => ({
          label: source.tag ? `${source.name} — ${source.tag}` : source.name,
          tag: source.tag ?? source.name,
          metadata: source.metadata,
        }))
        .filter((option) => option.tag),
    [sources],
  );

  const [selectedSourceTag, setSelectedSourceTag] = useState<string>(() => initialSourceFromQuery || sourceOptions[0]?.tag || '');
  const [lookbackSeconds] = useState<number>(300);
  const [analysis, setAnalysis] = useState<TimeshiftAnalysis | null>(null);
  const [processing, setProcessing] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [viewDuration, setViewDuration] = useState<number>(30);
  const [viewOffset, setViewOffset] = useState<number>(0);
  const [selection, setSelection] = useState<TimeRange | null>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [fingerprintColumns, setFingerprintColumns] = useState<number[]>([]);
  const audioContextRef = useRef<AudioContext | null>(null);
  const audioSourceRef = useRef<AudioBufferSourceNode | null>(null);

  const stopPlayback = useCallback(() => {
    if (audioSourceRef.current) {
      try {
        audioSourceRef.current.stop();
      } catch (error) {
        console.warn('Failed to stop audio playback', error);
      }
      audioSourceRef.current.disconnect();
      audioSourceRef.current = null;
    }
    setIsPlaying(false);
  }, []);

  const selectedSource = useMemo(
    () => sources.find((source) => (source.tag ?? source.name) === selectedSourceTag) ?? null,
    [sources, selectedSourceTag],
  );

  useEffect(() => {
    if (!sourceOptions.length) {
      return;
    }
    const matchesCurrent = selectedSourceTag && sourceOptions.some((option) => option.tag === selectedSourceTag);
    if (!matchesCurrent) {
      const fallbackTag = initialSourceFromQuery && sourceOptions.some(option => option.tag === initialSourceFromQuery)
        ? initialSourceFromQuery
        : sourceOptions[0].tag;
      setSelectedSourceTag(fallbackTag);
    }
  }, [sourceOptions, selectedSourceTag, initialSourceFromQuery]);

  useEffect(() => {
    if (!analysis) {
      setSelection(null);
    } else {
      const duration = analysis.recording.durationSeconds;
      setViewDuration((prev) => clamp(prev, 0.5, duration));
      setViewOffset((prev) => clamp(prev, 0, Math.max(0, duration - viewDuration)));
    }
  }, [analysis, viewDuration]);

  const loadRecording = useCallback(async () => {
    if (!selectedSourceTag) {
      setError('No source selected.');
      return;
    }
    setProcessing(true);
    setError(null);
    try {
      const recording = await fetchTimeshiftRecording(selectedSourceTag, lookbackSeconds);
      await new Promise((resolve) => requestAnimationFrame(resolve));
      const processed = convertRecordingToAnalysis(recording);
      setAnalysis(processed);
      const duration = processed.recording.durationSeconds;
      const initialView = clamp(30, 0.5, duration);
      setViewDuration(initialView);
      setViewOffset(0);
      setSelection(null);
      stopPlayback();
    } catch (err) {
      console.error('Failed to fetch timeshift buffer:', err);
      setAnalysis(null);
      if (err instanceof Error) {
        setError(err.message);
      } else {
        setError('Failed to fetch timeshift buffer.');
      }
    } finally {
      setProcessing(false);
    }
  }, [selectedSourceTag, lookbackSeconds, stopPlayback]);

  const duration = analysis?.recording.durationSeconds ?? 0;
  const viewEnd = clamp(viewOffset + viewDuration, 0, duration);
  const viewRange: TimeRange = {
    start: clamp(viewOffset, 0, Math.max(0, duration - 0.001)),
    end: Math.max(viewOffset + 0.001, viewEnd),
  };

  const handleSelectionChange = (range: TimeRange | null) => {
    if (!range) {
      setSelection(null);
      return;
    }
    const normalizedStart = clamp(range.start, 0, duration);
    const normalizedEnd = clamp(range.end, normalizedStart, duration);
    setSelection({ start: normalizedStart, end: normalizedEnd });
  };

  const downloadClip = (range: TimeRange | null) => {
    if (!analysis) return;
    const clipRange: TimeRange = range && range.end > range.start
      ? range
      : { start: 0, end: analysis.recording.durationSeconds };

    const pcmWindow = extractPCMWindow(analysis, clipRange.start, clipRange.end);
    const blob = buildWavBlob(
      pcmWindow,
      analysis.recording.sampleRate,
      analysis.recording.channels,
      analysis.recording.bitDepth,
    );
    const url = URL.createObjectURL(blob);
    const fragment = sanitizeFilenameFragment(selectedSourceTag);
    const filename = `${fragment}_${clipRange.start.toFixed(2)}-${clipRange.end.toFixed(2)}.wav`;
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  };

  const playSelection = useCallback(() => {
    if (!analysis || !selection || selection.end <= selection.start) {
      return;
    }

    const { recording, monoSamples } = analysis;
    const { sampleRate } = recording;

    const startFrame = Math.max(0, Math.floor(selection.start * sampleRate));
    const endFrame = Math.min(monoSamples.length, Math.ceil(selection.end * sampleRate));
    const frameCount = Math.max(0, endFrame - startFrame);
    if (frameCount === 0) {
      return;
    }

    let context = audioContextRef.current;
    if (!context || context.state === 'closed') {
      const AudioContextClass = window.AudioContext || (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
      if (!AudioContextClass) {
        console.error('Web Audio API is not supported in this browser.');
        return;
      }
      context = new AudioContextClass();
      audioContextRef.current = context;
    }

    if (context.state === 'suspended') {
      void context.resume();
    }

    stopPlayback();

    const buffer = context.createBuffer(1, frameCount, sampleRate);
    const slice = monoSamples.slice(startFrame, endFrame);
    if (typeof buffer.copyToChannel === 'function') {
      buffer.copyToChannel(slice, 0);
    } else {
      buffer.getChannelData(0).set(slice);
    }

    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(context.destination);
    source.onended = () => {
      setIsPlaying(false);
      audioSourceRef.current = null;
    };
    source.start(0);
    audioSourceRef.current = source;
    setIsPlaying(true);
  }, [analysis, selection, stopPlayback]);

  useEffect(() => () => {
    stopPlayback();
    const context = audioContextRef.current;
    if (context && context.state !== 'closed') {
      context.close().catch(() => null);
    }
    audioContextRef.current = null;
  }, [stopPlayback]);

  useEffect(() => {
    if (!analysis) {
      setFingerprintColumns([]);
      return;
    }
    const matrix = computeSelectionFingerprint(analysis, selection);
    const columnMasks = matrix.map((column) => column.reduce((mask, bit, row) => {
      if (bit) {
        const shifted = (1 << row) >>> 0;
        return (mask | shifted) >>> 0;
      }
      return mask;
    }, 0));
    setFingerprintColumns(columnMasks);
    console.debug('Fingerprint columns generated', {
      selection,
      columns: columnMasks.length,
      sample: columnMasks.slice(0, 6),
    });
  }, [analysis, selection]);

  const metadataBg = useColorModeValue('gray.50', 'whiteAlpha.50');
  const selectionDuration = selection ? selection.end - selection.start : 0;
  const selectionIsPlayable = selectionDuration > 0.01;
  const handleZoom = useCallback((focusTime: number, deltaY: number) => {
    if (!analysis || duration <= 0) {
      return;
    }
    const zoomFactor = deltaY > 0 ? 1.1 : 0.9;
    const minDuration = Math.min(0.5, duration) || 0.05;
    const maxDuration = Math.max(minDuration, duration);
    const newDuration = clamp(viewDuration * zoomFactor, minDuration, maxDuration);
    const half = newDuration / 2;
    const center = clamp(focusTime, 0, duration);
    const maxOffset = Math.max(0, duration - newDuration);
    const newOffset = clamp(center - half, 0, maxOffset);
    setViewDuration(newDuration);
    setViewOffset(newOffset);
  }, [analysis, duration, viewDuration]);

  const handlePan = useCallback((deltaSeconds: number) => {
    if (!analysis || duration <= viewDuration) {
      return;
    }
    const maxOffset = Math.max(0, duration - viewDuration);
    setViewOffset((prev) => clamp(prev + deltaSeconds, 0, maxOffset));
  }, [analysis, duration, viewDuration]);

  const copyFingerprintToClipboard = useCallback(async () => {
    if (!fingerprintColumns.length) {
      toast({
        title: 'No fingerprint available',
        description: 'Select audio to generate a fingerprint first.',
        status: 'warning',
        duration: 2500,
        isClosable: true,
      });
      return;
    }

    try {
      await navigator.clipboard.writeText(JSON.stringify(fingerprintColumns));
      toast({
        title: 'Fingerprint copied',
        description: 'JSON array copied to clipboard.',
        status: 'success',
        duration: 2000,
        isClosable: true,
      });
    } catch (error) {
      console.error('Failed to copy fingerprint', error);
      toast({
        title: 'Copy failed',
        description: 'Clipboard access was blocked.',
        status: 'error',
        duration: 2500,
        isClosable: true,
      });
    }
  }, [fingerprintColumns, toast]);

  useEffect(() => {
    loadRecording().catch((err) => {
      console.error('Failed to load timeshift buffer', err);
    });
  // loadRecording uses selectedSourceTag and lookbackSeconds, but lookbackSeconds is constant
  // we intentionally do not include loadRecording itself to avoid ref churn
  }, [selectedSourceTag, lookbackSeconds]);

  return (
    <Box px={6} py={8} w="100%">
      <Stack spacing={6} w="100%">
        <Heading size="lg">Timeshift Viewer</Heading>

        {error && (
          <Alert status="error">
            <AlertIcon />
            {error}
          </Alert>
        )}

        {processing && (
          <Flex align="center" justify="center" py={10}>
            <Spinner size="xl" />
          </Flex>
        )}

        {analysis && !processing && (
          <Stack spacing={6}>
            <Card bg={metadataBg} w="full" maxW="none" style={{"display": "none"}}>
              <CardHeader pb={2} w="full">
                <Heading size="md">Recording Metadata</Heading>
              </CardHeader>
              <CardBody pt={0} w="full">
                <Grid templateColumns={{ base: 'repeat(2, 1fr)', md: 'repeat(4, 1fr)' }} gap={4}>
                  <Stat>
                    <StatLabel>Duration</StatLabel>
                    <StatNumber>{formatSeconds(duration)}</StatNumber>
                  </Stat>
                  <Stat>
                    <StatLabel>Sample Rate</StatLabel>
                    <StatNumber>{analysis.recording.sampleRate.toLocaleString()} Hz</StatNumber>
                  </Stat>
                  <Stat>
                    <StatLabel>Channels</StatLabel>
                    <StatNumber>{analysis.recording.channels}</StatNumber>
                  </Stat>
                  <Stat>
                    <StatLabel>Bit Depth</StatLabel>
                    <StatNumber>{analysis.recording.bitDepth}</StatNumber>
                  </Stat>
                </Grid>
              </CardBody>
            </Card>

            <Card w="full" maxW="none">
              <CardBody w="full">
                <VStack align="stretch" spacing={4} w="full">
                  <Flex direction={{ base: 'column', md: 'row' }} style={{"display": "none"}} gap={4}>
                    <FormControl>
                      <FormLabel>View Window (seconds)</FormLabel>
                      <NumberInput
                        min={0.5}
                        max={Math.max(0.5, duration)}
                        step={0.5}
                        value={viewDuration}
                        onChange={(_, value) => {
                          const clamped = clamp(value, 0.5, Math.max(0.5, duration));
                          setViewDuration(clamped);
                          setViewOffset((prev) => clamp(prev, 0, Math.max(0, duration - clamped)));
                        }}
                      >
                        <NumberInputField />
                        <NumberInputStepper>
                          <NumberIncrementStepper />
                          <NumberDecrementStepper />
                        </NumberInputStepper>
                      </NumberInput>
                    </FormControl>
                    <FormControl>
                      <FormLabel>View Start (seconds)</FormLabel>
                      <NumberInput
                        min={0}
                        max={Math.max(0, duration - viewDuration)}
                        step={0.5}
                        value={viewOffset}
                        onChange={(_, value) => {
                          setViewOffset(clamp(value, 0, Math.max(0, duration - viewDuration)));
                        }}
                      >
                        <NumberInputField />
                        <NumberInputStepper>
                          <NumberIncrementStepper />
                          <NumberDecrementStepper />
                        </NumberInputStepper>
                      </NumberInput>
                    </FormControl>
                  </Flex>
                  <Box>
                    <FormLabel>Navigate</FormLabel>
                    <Slider
                      min={0}
                      max={Math.max(0, duration - viewDuration)}
                      step={Math.max(0.01, viewDuration / 200)}
                      value={viewOffset}
                      onChange={(value) => setViewOffset(value)}
                    >
                      <SliderTrack>
                        <SliderFilledTrack />
                      </SliderTrack>
                      <SliderThumb />
                    </Slider>
                  </Box>
                  <Divider />
                  <TimeshiftViewer
                    analysis={analysis}
                    viewRange={viewRange}
                    selection={selection}
                    onSelectionChange={handleSelectionChange}
                    onZoom={handleZoom}
                    onPan={handlePan}
                  />
                  <Flex justify="space-between" fontSize="sm" color="gray.500">
                    <Text>Window start: {viewRange.start.toFixed(2)}s</Text>
                    <Text>Window end: {viewRange.end.toFixed(2)}s</Text>
                  </Flex>
                  <Flex
                    direction={{ base: 'column', md: 'row' }}
                    gap={3}
                    align={{ base: 'flex-start', md: 'center' }}
                    justify="space-between"
                    fontSize="sm"
                    color="gray.500"
                  >
                    <Box>
                      {selection
                        ? (
                          <Text>
                            Selected: {selection.start.toFixed(2)}s – {selection.end.toFixed(2)}s (
                            {formatSeconds(selectionDuration)}
                            )
                          </Text>
                        )
                        : <Text>Click and drag on the waveform to select a clip. Double-click to clear.</Text>}
                    </Box>
                    <HStack>
                      <Tooltip label={selectionIsPlayable ? 'Play current selection' : 'Select a region to enable playback'}>
                        <Button
                          size="sm"
                          colorScheme={isPlaying ? 'red' : 'blue'}
                          leftIcon={<Icon as={isPlaying ? FaStop : FaPlay} />}
                          onClick={() => {
                            if (isPlaying) {
                              stopPlayback();
                            } else {
                              playSelection();
                            }
                          }}
                          isDisabled={!selectionIsPlayable}
                        >
                          {isPlaying ? 'Stop' : 'Play Selection'}
                        </Button>
                      </Tooltip>
                      <Tooltip
                        label={selection ? 'Download highlighted clip as WAV' : 'Download full timeshift buffer'}
                        hasArrow
                      >
                        <Button
                          size="sm"
                          leftIcon={<Icon as={FaDownload} />}
                          colorScheme="teal"
                          onClick={() => downloadClip(selection)}
                        >
                          Download {selection ? 'Selection' : 'Buffer'}
                        </Button>
                      </Tooltip>
                      <Tooltip
                        label={fingerprintColumns.length ? 'Copy current selection fingerprint as JSON' : 'Select audio to generate a fingerprint'}
                      >
                        <Button
                          size="sm"
                          variant="outline"
                          leftIcon={<Icon as={FaClipboard} />}
                          onClick={copyFingerprintToClipboard}
                          isDisabled={!fingerprintColumns.length}
                        >
                          Copy Fingerprint
                        </Button>
                      </Tooltip>
                    </HStack>
                  </Flex>
                </VStack>
              </CardBody>
            </Card>
          </Stack>
        )}
      </Stack>
    </Box>
  );
};

export default TimeshiftPage;

const clamp = (value: number, min: number, max: number): number => Math.min(Math.max(value, min), max);

const FINGERPRINT_MIN_FREQ = 300;
const FINGERPRINT_MAX_FREQ = 2000;
const FINGERPRINT_ROWS = 32;
const FINGERPRINT_THRESHOLD_SEED = 0.35;
const FINGERPRINT_DYNAMIC_TWEAK = 0.1;
const EPSILON = 1e-6;

function computeSelectionFingerprint(analysis: TimeshiftAnalysis, selection: TimeRange | null): number[][] {
  const {
    spectrogram,
    recording: { sampleRate, durationSeconds },
  } = analysis;

  if (!spectrogram || spectrogram.timeBins === 0 || spectrogram.freqBins === 0) {
    return [];
  }

  const startTime = selection ? clamp(selection.start, 0, durationSeconds) : 0;
  const endTime = selection ? clamp(selection.end, startTime, durationSeconds) : durationSeconds;
  if (endTime <= startTime) {
    return [];
  }

  const binDuration = spectrogram.hopSize / sampleRate;
  const startBin = clamp(Math.floor(startTime / binDuration), 0, spectrogram.timeBins - 1);
  const endBin = clamp(Math.ceil(endTime / binDuration), startBin + 1, spectrogram.timeBins);
  const totalBins = endBin - startBin;
  if (totalBins <= 0) {
    return [];
  }

  const freqResolution = sampleRate / spectrogram.fftSize;
  const minFreqBin = clamp(Math.floor(FINGERPRINT_MIN_FREQ / freqResolution), 0, spectrogram.freqBins - 1);
  const maxFreqBin = clamp(Math.ceil(FINGERPRINT_MAX_FREQ / freqResolution), minFreqBin + 1, spectrogram.freqBins);
  const freqRange = maxFreqBin - minFreqBin;
  if (freqRange <= 0) {
    return [];
  }

  const columns = Math.max(1, totalBins);
  const rows = FINGERPRINT_ROWS;

  const matrix: number[][] = Array.from({ length: columns }, () => Array(rows).fill(0));
  const magnitudeSpan = (spectrogram.maxMagnitude - spectrogram.minMagnitude) || EPSILON;
  let totalMagnitude = 0;
  let magnitudeSamples = 0;

  for (let column = 0; column < columns; column += 1) {
    const columnStartBin = startBin + Math.floor((totalBins * column) / columns);
    let columnEndBin = startBin + Math.floor((totalBins * (column + 1)) / columns);
    if (columnEndBin <= columnStartBin) {
      columnEndBin = Math.min(columnStartBin + 1, endBin);
    }

    for (let row = 0; row < rows; row += 1) {
      const rowStartFreq = minFreqBin + Math.floor((freqRange * row) / rows);
      let rowEndFreq = minFreqBin + Math.floor((freqRange * (row + 1)) / rows);
      if (rowEndFreq <= rowStartFreq) {
        rowEndFreq = Math.min(rowStartFreq + 1, maxFreqBin);
      }

      let sum = 0;
      let count = 0;
      for (let t = columnStartBin; t < columnEndBin; t += 1) {
        const timeOffset = t * spectrogram.freqBins;
        for (let f = rowStartFreq; f < rowEndFreq; f += 1) {
          sum += spectrogram.data[timeOffset + f];
          count += 1;
        }
      }

      if (count > 0) {
        const averageMagnitude = sum / count;
        let normalized = (averageMagnitude - spectrogram.minMagnitude) / magnitudeSpan;
        normalized = Math.max(0, Math.min(1, normalized));
        totalMagnitude += normalized;
        magnitudeSamples += 1;
        matrix[column][row] = Math.max(matrix[column][row], normalized);
      }
    }
  }

  const averageNormalizedMagnitude = magnitudeSamples > 0 ? totalMagnitude / magnitudeSamples : 0;
  const globalThreshold = Math.min(
    1,
    Math.max(0, averageNormalizedMagnitude + FINGERPRINT_THRESHOLD_SEED),
  ) + FINGERPRINT_DYNAMIC_TWEAK;

  return matrix.map((columnValues) => {
    const columnMax = columnValues.reduce((max, value) => Math.max(max, value), 0);
    const columnThreshold = columnMax > 0 ? columnMax * 0.65 : globalThreshold;
    const finalThreshold = Math.max(globalThreshold * 0.5, columnThreshold);
    return columnValues.map((value) => (value >= finalThreshold ? 1 : 0));
  });
}
